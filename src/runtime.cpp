#include "runtime.h"
#include "paths.h"
#include "files.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace nsq {
namespace {
// 位于每次分配之前的长度头；对齐保证其后的引擎数据满足普通类型要求。
struct alignas(std::max_align_t) Allocation {
    // 有效载荷字节数，用于配额统计、查询及 realloc 差额计算。
    size_t size;
};
// 配置中的 MiB 转换为字节，使用二进制单位。
constexpr size_t MiB = 1024 * 1024;
}

void* Runtime::allocate(void* opaque, size_t size) {
    auto& self = *static_cast<Runtime*>(opaque);
    // 同时检查头部加法溢出和剩余配额；饱和减法避免初始化后降低配额造成下溢。
    if (size > SIZE_MAX - sizeof(Allocation) || size > self.limit_ - std::min(self.used_, self.limit_)) {
        self.oom_ = true;
        return nullptr;
    }
    auto block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!block) { self.oom_ = true; return nullptr; }
    block->size = size;
    self.used_ += size;
    return block + 1;
}
void* Runtime::allocateZero(void* opaque, size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        static_cast<Runtime*>(opaque)->oom_ = true;
        return nullptr;
    }
    void* ptr = allocate(opaque, count * size);
    if (ptr) std::memset(ptr, 0, count * size);
    return ptr;
}
size_t Runtime::allocationSize(const void* ptr) {
    return ptr ? (static_cast<const Allocation*>(ptr) - 1)->size : 0;
}
void Runtime::deallocate(void* opaque, void* ptr) {
    if (!ptr) return;
    auto& self = *static_cast<Runtime*>(opaque);
    self.used_ -= allocationSize(ptr);
    std::free(static_cast<Allocation*>(ptr) - 1);
}
void* Runtime::reallocate(void* opaque, void* ptr, size_t size) {
    if (!ptr) return allocate(opaque, size);
    if (!size) { deallocate(opaque, ptr); return nullptr; }
    auto& self = *static_cast<Runtime*>(opaque);
    size_t old = allocationSize(ptr);
    if (size > SIZE_MAX - sizeof(Allocation) ||
        (size > old && size - old > self.limit_ - std::min(self.used_, self.limit_))) {
        self.oom_ = true;
        return nullptr;
    }
    // realloc 失败时旧块及用量保持不变，调用方仍负责释放原指针。
    auto block = static_cast<Allocation*>(std::realloc(static_cast<Allocation*>(ptr) - 1, sizeof(Allocation) + size));
    if (!block) { self.oom_ = true; return nullptr; }
    block->size = size;
    self.used_ = self.used_ - old + size;
    return block + 1;
}

int Runtime::interrupt(JSRuntime*, void* opaque) {
    auto& self = *static_cast<Runtime*>(opaque);
    if (self.active_ && std::chrono::steady_clock::now() >= self.deadline_) self.timedOut_ = true;
    return self.timedOut_ || self.oom_;
}
void Runtime::checkLimits() {
    interrupt(rt_, this);
    if (oom_) { invalid_ = true; throw Fault("E_MEMORY", "JS 内存上限已触发，请销毁并重新初始化"); }
    if (timedOut_) { invalid_ = true; throw Fault("E_TIMEOUT", "JS 执行超时，请销毁并重新初始化"); }
}
std::string Runtime::text(JSValueConst value) {
    size_t length = 0;
    const char* str = JS_ToCStringLen(ctx_, &length, value);
    if (!str) {
        Value ignored(ctx_, JS_GetException(ctx_));
        checkLimits();
        throw Fault("E_JS", "无法转换 JS 文本");
    }
    try {
        std::string result(str, length);
        JS_FreeCString(ctx_, str);
        return result;
    } catch (...) { JS_FreeCString(ctx_, str); throw; }
}
std::string Runtime::describe(JSValueConst value) {
    std::string message = text(value);
    if (JS_IsError(value)) {
        Value stack(ctx_, JS_GetPropertyStr(ctx_, value, "stack"));
        if (!JS_IsException(stack) && JS_IsString(stack)) message += "\n" + text(stack);
        else if (JS_IsException(stack)) { Value ignored(ctx_, JS_GetException(ctx_)); }
    }
    return message;
}
void Runtime::checked(JSValueConst value) {
    if (JS_IsException(value)) {
        Value exception(ctx_, JS_GetException(ctx_));
        checkLimits();
        auto message = describe(exception);
        checkLimits();
        throw Fault("E_JS", message);
    }
    checkLimits();
}

void Runtime::init(const std::string& json) {
    // 引擎持有的分配器函数表；静态存储期覆盖所有 Runtime 实例。
    static const JSMallocFunctions allocators = {allocateZero, allocate, deallocate, reallocate, allocationSize};
    limit_ = 64 * MiB;
    timeoutMs_ = 5000;
    invalid_ = timedOut_ = oom_ = false;
    allowFileAccess_ = false;
    rt_ = JS_NewRuntime2(&allocators, this);
    if (!rt_) throw Fault("E_MEMORY", "无法创建 JS 运行时");
#ifdef _DEBUG
    JS_SetDumpFlags(rt_, JS_DUMP_LEAKS);
#endif
    JS_SetMaxStackSize(rt_, 256 * 1024);
    JS_SetCanBlock(rt_, false);
    JS_SetInterruptHandler(rt_, interrupt, this);
    ctx_ = JS_NewContext(rt_);
    if (!ctx_) throw Fault("E_MEMORY", "无法创建 JS 上下文");
    // 回调使用宿主对象检查权限，权限值不保存在可由 JS 修改的属性中。
    JS_SetContextOpaque(ctx_, this);
    Value config(ctx_, JS_ParseJSON(ctx_, json.data(), json.size(), "<config>"));
    checked(config);
    if (!JS_IsObject(config) || JS_IsArray(config)) throw Fault("E_ARGUMENT", "配置必须是 JSON 对象");
    Value root(ctx_, JS_GetPropertyStr(ctx_, config, "rootDir"));
    if (!JS_IsString(root)) throw Fault("E_ARGUMENT", "rootDir 必须是绝对目录路径");
    auto rootName = text(root);
    if (rootName.find('\0') != std::string::npos) throw Fault("E_PATH", "目录包含空字符");
    auto input = std::filesystem::path(utf16(rootName));
    if (!input.is_absolute()) throw Fault("E_PATH", "rootDir 必须是绝对目录路径");
    std::error_code error;
    root_ = std::filesystem::canonical(input, error);
    if (error || !std::filesystem::is_directory(root_, error) || error)
        throw Fault("E_PATH", "rootDir 目录不存在");
    // 读取可选整数配置；缺省使用 fallback，拒绝小数、非有限数及越界值。
    auto integerOption = [&](const char* name, int fallback, int min, int max) {
        Value option(ctx_, JS_GetPropertyStr(ctx_, config, name));
        if (JS_IsUndefined(option)) return fallback;
        double value;
        if (!JS_IsNumber(option) || JS_ToFloat64(ctx_, &value, option) < 0 ||
            !(value >= min && value <= max) || value != static_cast<int>(value))
            throw Fault("E_ARGUMENT", std::string(name) + " 超出整数范围");
        return static_cast<int>(value);
    };
    timeoutMs_ = integerOption("timeoutMs", 5000, 1, 600000);
    limit_ = static_cast<size_t>(integerOption("memoryLimitMiB", 64, 1, 1024)) * MiB;
    Value access(ctx_, JS_GetPropertyStr(ctx_, config, "allowFileAccess"));
    checked(access);
    if (!JS_IsUndefined(access)) {
        if (!JS_IsBool(access)) throw Fault("E_ARGUMENT", "allowFileAccess 必须是布尔值");
        allowFileAccess_ = JS_ToBool(ctx_, access) != 0;
    }
    Value fs(ctx_, createFileApi(ctx_, fileOperation));
    checked(fs);
    Value global(ctx_, JS_GetGlobalObject(ctx_));
    checked(global);
    if (JS_DefinePropertyValueStr(ctx_, global, "fs", fs.release(), JS_PROP_ENUMERABLE) < 0) checked(JS_EXCEPTION);
    JS_SetModuleLoaderFunc(rt_, normalize, load, this);
    JS_SetHostPromiseRejectionTracker(rt_, rejection, this);
}

JSValue Runtime::fileOperation(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int operation) {
    auto* self = static_cast<Runtime*>(JS_GetContextOpaque(ctx));
    if (!self) return JS_ThrowInternalError(ctx, "文件接口缺少宿主上下文");
    try {
        self->checkLimits();
        if (!self->allowFileAccess_) throw Fault("E_PERMISSION", "文件访问未启用，请由 NSIS 开启权限");
        Value result(ctx, executeFileOperation(ctx, self->root_, operation, argc, argv));
        // 同步 Windows I/O 无法被 JS 中断，返回引擎前补查执行期限。
        self->checkLimits();
        return result.release();
    } catch (const Fault& error) {
        return throwFileError(ctx, error.code.c_str(), error.what());
    } catch (const std::bad_alloc&) {
        self->oom_ = true;
        return JS_ThrowOutOfMemory(ctx);
    } catch (const std::exception& error) {
        return throwFileError(ctx, "E_IO", error.what());
    } catch (...) {
        return JS_ThrowInternalError(ctx, "文件操作内部错误");
    }
}

void Runtime::clearRejections() noexcept {
    for (auto& pair : rejections_) {
        JS_FreeValue(ctx_, pair.first);
        JS_FreeValue(ctx_, pair.second);
    }
    rejections_.clear();
}
void Runtime::destroy() noexcept {
    active_ = false;
    allowFileAccess_ = false;
    if (ctx_) {
        // 先停止拒绝通知，防止释放引用过程中重新写入跟踪表。
        JS_SetHostPromiseRejectionTracker(rt_, nullptr, nullptr);
        clearRejections();
        for (auto& pair : modules_) JS_FreeValue(ctx_, pair.second);
        modules_.clear();
        // 先回收悬空异步闭包，确保其模块引用释放时模块定义仍然有效。
        JS_RunGC(rt_);
        JS_FreeContext(ctx_);
        ctx_ = nullptr;
    }
    if (rt_) { JS_FreeRuntime(rt_); rt_ = nullptr; }
    assert(used_ == 0);
    invalid_ = timedOut_ = oom_ = false;
    used_ = 0;
}

void Runtime::rejection(JSContext* ctx, JSValueConst promise, JSValueConst reason, bool handled, void* opaque) {
    auto& self = *static_cast<Runtime*>(opaque);
    // 以 Promise 对象身份匹配，后续 catch 通知移除同一条记录。
    auto it = std::find_if(self.rejections_.begin(), self.rejections_.end(), [&](const auto& pair) {
        return JS_VALUE_GET_PTR(pair.first) == JS_VALUE_GET_PTR(promise);
    });
    if (handled) {
        if (it != self.rejections_.end()) {
            JS_FreeValue(ctx, it->first);
            JS_FreeValue(ctx, it->second);
            self.rejections_.erase(it);
        }
    } else if (it == self.rejections_.end()) {
        // 先保证容器插入成功再增加 JS 引用，宿主分配失败也不会泄漏引用。
        try { self.rejections_.emplace_back(promise, reason); }
        catch (...) { self.oom_ = true; return; }
        JS_DupValue(ctx, promise);
        JS_DupValue(ctx, reason);
    }
}
void Runtime::drain() {
    while (JS_IsJobPending(rt_)) {
        checkLimits();
        JSContext* context = nullptr;
        if (JS_ExecutePendingJob(rt_, &context) < 0) checked(JS_EXCEPTION);
    }
    checkLimits();
}
Value Runtime::settle(Value value) {
    checked(value);
    const bool promise = JS_IsPromise(value);
    // 返回的 Promise 由宿主接管拒绝处理，避免作为未处理拒绝重复报告。
    if (promise) JS_PromiseMarkAsHandled(ctx_, value);
    drain();
    if (promise) {
        auto state = JS_PromiseState(ctx_, value);
        // 无计时器或外部事件循环；队列已空仍 pending 时无法继续推进。
        if (state == JS_PROMISE_PENDING) {
            invalid_ = true;
            throw Fault("E_PENDING", "Promise 尚未完成且没有可执行任务，请销毁并重新初始化");
        }
        Value result(ctx_, JS_PromiseResult(ctx_, value));
        if (state == JS_PROMISE_REJECTED) throw Fault("E_JS", describe(result));
        return result;
    }
    return value;
}
Value Runtime::evaluate(const std::string& source, const std::string& filename) {
    return settle(Value(ctx_, JS_Eval(ctx_, source.data(), source.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL)));
}
std::string Runtime::serialize(JSValueConst value) {
    if (JS_IsUndefined(value)) return {};
    Value json(ctx_, JS_JSONStringify(ctx_, value, JS_UNDEFINED, JS_UNDEFINED));
    if (JS_IsException(json)) {
        Value exception(ctx_, JS_GetException(ctx_));
        checkLimits();
        throw Fault("E_SERIALIZE", describe(exception));
    }
    if (JS_IsUndefined(json)) throw Fault("E_SERIALIZE", "返回值不能序列化为 JSON");
    return text(json);
}

char* Runtime::normalize(JSContext* ctx, const char* base, const char* name, void* opaque) {
    auto& self = *static_cast<Runtime*>(opaque);
    try {
        self.checkLimits();
        std::string specifier(name);
        // 仅宿主生成的入口可使用绝对路径；模块内部依赖必须显式相对导入。
        bool entry = std::strcmp(base, "<module-entry>") == 0;
        if (!entry && specifier.rfind("./", 0) != 0 && specifier.rfind("../", 0) != 0)
            throw Fault("E_PATH", "模块依赖仅支持 ./ 或 ../ 相对路径");
        auto parent = entry ? self.root_ : std::filesystem::path(utf16(base)).parent_path();
        auto path = resolvePath(self.root_, parent, specifier);
        if (!path.has_extension()) throw Fault("E_PATH", "模块路径必须包含扩展名");
        return js_strdup(ctx, utf8(path.generic_wstring()).c_str());
    } catch (const std::exception& e) { JS_ThrowReferenceError(ctx, "%s", e.what()); return nullptr; }
    catch (...) { JS_ThrowInternalError(ctx, "模块路径解析失败"); return nullptr; }
}
JSModuleDef* Runtime::load(JSContext* ctx, const char* name, void* opaque) {
    auto& self = *static_cast<Runtime*>(opaque);
    try {
        self.checkLimits();
        auto path = resolvePath(self.root_, self.root_, name);
        auto source = readScript(path);
        Value compiled(ctx, JS_Eval(ctx, source.data(), source.size(), name,
            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY));
        if (JS_IsException(compiled)) return nullptr;
        // 编译值的临时引用释放后，模块定义仍由上下文的模块表持有。
        return static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(static_cast<JSValue>(compiled)));
    } catch (const std::exception& e) { JS_ThrowReferenceError(ctx, "%s", e.what()); return nullptr; }
    catch (...) { JS_ThrowInternalError(ctx, "模块加载失败"); return nullptr; }
}
Value Runtime::loadModule(const std::string& alias, const std::string& name) {
    if (alias.empty() || modules_.count(alias)) throw Fault("E_ARGUMENT", "模块别名为空或已存在");
    auto path = resolvePath(root_, root_, name);
    auto filename = utf8(path.generic_wstring());
    Value str(ctx_, JS_NewStringLen(ctx_, filename.data(), filename.size()));
    checked(str);
    // 用 JSON 字符串转义路径，避免引号或反斜杠改变动态导入源码。
    auto source = "import(" + serialize(str) + ")";
    Value module = evaluate(source, "<module-entry>");
    // 只有映射插入成功才移交引用；容器分配失败由 Value 自动释放。
    modules_.emplace(alias, static_cast<JSValue>(module));
    module.release();
    return Value(ctx_, JS_UNDEFINED);
}
Value Runtime::call(const std::vector<std::string>& args) {
    Value parameters(ctx_, JS_ParseJSON(ctx_, args[2].data(), args[2].size(), "<arguments>"));
    checked(parameters);
    if (!JS_IsArray(parameters)) throw Fault("E_ARGUMENT", "参数必须是 JSON 数组");
    if (args[1].empty() || args[1].find('\0') != std::string::npos)
        throw Fault("E_ARGUMENT", "函数名无效");
    JSValue object;
    if (args[0].empty()) object = JS_GetGlobalObject(ctx_);
    else {
        auto it = modules_.find(args[0]);
        if (it == modules_.end()) throw Fault("E_ARGUMENT", "模块别名不存在");
        object = JS_DupValue(ctx_, it->second);
    }
    Value owner(ctx_, object);
    Value function(ctx_, JS_GetPropertyStr(ctx_, owner, args[1].c_str()));
    checked(function);
    if (!JS_IsFunction(ctx_, function)) throw Fault("E_ARGUMENT", "目标不是可调用函数");
    Value length(ctx_, JS_GetPropertyStr(ctx_, parameters, "length"));
    uint32_t count = 0;
    JS_ToUint32(ctx_, &count, length);
    if (count > 1024) throw Fault("E_ARGUMENT", "最多传入 1024 个参数");
    // held 拥有参数引用，values 只提供 JS_Call 所需的连续借用数组。
    std::vector<Value> held;
    std::vector<JSValue> values;
    held.reserve(count); values.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        held.emplace_back(ctx_, JS_GetPropertyUint32(ctx_, parameters, i));
        checked(held.back());
        values.push_back(held.back());
    }
    return settle(Value(ctx_, JS_Call(ctx_, function, JS_UNDEFINED, static_cast<int>(count), values.data())));
}

Result Runtime::invoke(const std::string& operation, const std::vector<std::string>& args) {
    const size_t arity = operation == "Destroy" ? 0 : operation == "Call" ? 3 : operation == "LoadModule" ? 2 : 1;
    if (args.size() != arity) return {"E_ARGUMENT", "参数数量错误"};
    if (operation == "Destroy") { destroy(); return {}; }
    if (operation == "Init" && rt_) return {"E_STATE", "已经初始化，请先销毁上下文"};
    if (operation != "Init" && (!ctx_ || invalid_)) return {"E_STATE", "上下文未初始化或已失效"};
    deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs_);
    active_ = true;
    timedOut_ = false;
    try {
        if (operation == "Init") {
            init(args[0]);
            active_ = false;
            return {};
        }
        // 同一线程各次 ABI 调用的栈顶可能不同，执行前更新引擎栈限制基准。
        JS_UpdateStackTop(rt_);
        JSValue result;
        if (operation == "Eval") result = evaluate(args[0], utf8((root_ / L"__eval__.js").generic_wstring())).release();
        else if (operation == "EvalFile") {
            auto path = resolvePath(root_, root_, args[0]);
            result = evaluate(readScript(path), utf8(path.generic_wstring())).release();
        } else if (operation == "LoadModule") result = loadModule(args[0], args[1]).release();
        else if (operation == "Call") result = call(args).release();
        else if (operation == "SetFileAccess") {
            if (args[0] != "true" && args[0] != "false") throw Fault("E_ARGUMENT", "文件访问开关必须是 true 或 false");
            allowFileAccess_ = args[0] == "true";
            result = JS_UNDEFINED;
        }
        else throw Fault("E_ARGUMENT", "未知操作");
        Value output(ctx_, result);
        // toJSON 也可能执行 JS 或产生任务，因此序列化后仍需排空并检查限制。
        std::string json = serialize(output);
        drain();
        if (!rejections_.empty()) throw Fault("E_JS", "未处理的 Promise 拒绝：" + describe(rejections_.front().second));
        checkLimits();
        active_ = false;
        return {"0", std::move(json)};
    } catch (const Fault& error) {
        std::string code = error.code, message = error.what();
        // 资源错误优先于普通 JS 错误，即使脚本捕获异常也不能复用受损上下文。
        if (oom_) { invalid_ = true; code = "E_MEMORY"; message = "JS 内存上限已触发，请销毁并重新初始化"; }
        else if (timedOut_) { invalid_ = true; code = "E_TIMEOUT"; message = "JS 执行超时，请销毁并重新初始化"; }
        // 异常遗留的任务不能泄漏到下一次调用。
        if (rt_ && JS_IsJobPending(rt_)) invalid_ = true;
        active_ = false;
        if (ctx_) clearRejections();
        if (operation == "Init") destroy();
        return {code, message};
    } catch (const std::exception& error) {
        active_ = false;
        invalid_ = true;
        if (operation == "Init") destroy();
        return {"E_INTERNAL", error.what()};
    }
}
}
