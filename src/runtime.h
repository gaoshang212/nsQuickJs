#pragma once
#include <quickjs.h>
#include <chrono>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace nsq {
/// 将可预期的插件错误码与可读消息一起传递到调用边界。
struct Fault : std::runtime_error {
    /// 对外返回的稳定错误码，例如 E_ARGUMENT；消息由基类保存。
    std::string code;
    /// 接管错误码和消息；由 Runtime::invoke 转换为 Result。
    Fault(std::string code, std::string message) : std::runtime_error(std::move(message)), code(std::move(code)) {}
};
/// 一次插件调用的结果；经 ABI 层转换后压入 NSIS 栈。
struct Result {
    /// "0" 表示成功，其他值为 E_* 错误码。
    std::string code = "0";
    /// 成功时为 JSON（undefined 对应空串），失败时为错误说明。
    std::string text;
};

/// 独占一个 JSValue 引用；所属上下文必须比本对象存活更久。
class Value {
    /// 借用的上下文，用于释放引用，不负责销毁上下文。
    JSContext* ctx_;
    /// 当前持有的值；移交所有权后置为 JS_UNDEFINED。
    JSValue value_;
public:
    /// 接管已有引用，不增加引用计数；借用值须先 JS_DupValue。
    Value(JSContext* ctx, JSValue value) : ctx_(ctx), value_(value) {}
    /// 释放持有的引用，保证异常路径也能清理 JS 对象。
    ~Value() { JS_FreeValue(ctx_, value_); }
    /// 禁止复制，避免同一引用被释放两次。
    Value(const Value&) = delete;
    /// 禁止复制赋值，保持引用的独占所有权。
    Value& operator=(const Value&) = delete;
    /// 转移引用并清空源对象，供返回值和容器移动使用。
    Value(Value&& other) noexcept : ctx_(other.ctx_), value_(other.value_) { other.value_ = JS_UNDEFINED; }
    /// 提供借用值；调用者不得直接释放或交给会接管引用的 API。
    operator JSValue() const { return value_; }
    /// 移交引用；调用者负责最终释放或交给另一个所有者。
    JSValue release() { auto value = value_; value_ = JS_UNDEFINED; return value; }
};

/// 管理一个跨调用保留状态的 JS 上下文；由 ABI 层保证单线程、无重入。
class Runtime {
public:
    /// 创建未初始化的宿主对象，首次 Init 时才分配 JS 运行时。
    Runtime() = default;
    /// 按依赖顺序释放上下文与运行时，可在未初始化时调用。
    ~Runtime() { destroy(); }
    /// 禁止复制含有引擎指针及回调绑定的运行时。
    Runtime(const Runtime&) = delete;
    /// 禁止复制赋值，避免重复销毁和回调指向错误实例。
    Runtime& operator=(const Runtime&) = delete;
    /// 分派操作及有序 UTF-8 参数，将执行结果或已捕获异常转换为 Result。
    Result invoke(const std::string& operation, const std::vector<std::string>& args);
    /// 幂等清理所有 JS 引用和引擎资源；不向卸载回调抛出异常。
    void destroy() noexcept;
private:
    /// 拥有的引擎运行时；须在 ctx_ 及全部 JSValue 释放后销毁。
    JSRuntime* rt_ = nullptr;
    /// 拥有的全局上下文，保存跨调用的变量和模块状态。
    JSContext* ctx_ = nullptr;
    /// 已规范化的根目录，约束脚本加载、模块导入及 fs 文件读写。
    std::filesystem::path root_;
    /// 宿主授予的文件读写权限，默认关闭；每次 fs 调用都重新检查。
    bool allowFileAccess_ = false;
    /// 模块别名到命名空间的映射；每个值持有一个 JS 引用。
    std::map<std::string, JSValue> modules_;
    /// 未处理拒绝的 Promise/原因对；两者均持有引用，处理或销毁时释放。
    std::vector<std::pair<JSValue, JSValue>> rejections_;
    /// 上下文已失效，只允许 Destroy 后重新 Init。
    bool invalid_ = false,
        /// 本次调用已触发截止时间，中断回调持续返回终止信号。
        timedOut_ = false,
        /// 分配失败或超出配额；JS 捕获异常也不能清除此标记。
        oom_ = false,
        /// 当前处于受超时保护的调用期间，销毁时关闭。
        active_ = false;
    /// 自定义分配器累计的有效载荷字节数，不含 Allocation 头部。
    size_t used_ = 0,
        /// JS 分配配额，默认 64 MiB；不包含宿主 C++ 容器内存。
        limit_ = 64 * 1024 * 1024;
    /// 后续调用的执行时限（毫秒），由 Init 配置覆盖。
    int timeoutMs_ = 5000;
    /// 当前调用的单调时钟截止时间，包含任务队列和结果序列化耗时。
    std::chrono::steady_clock::time_point deadline_;
    /// 解析配置、校验根目录并安装引擎回调；失败由 invoke 清理半初始化资源。
    void init(const std::string& json);
    /// 检查内存和时间限制，超限时标记失效并抛出 E_MEMORY/E_TIMEOUT。
    void checkLimits();
    /// 检查 JS_EXCEPTION 和资源限制，将引擎异常转换为 Fault。
    void checked(JSValueConst value);
    /// 将借用值转换为 UTF-8 文本，保留长度并释放引擎分配的字符串。
    std::string text(JSValueConst value);
    /// 生成错误描述；Error 对象附加可读取的 stack 属性。
    std::string describe(JSValueConst value);
    /// 将值序列化为 JSON；undefined 返回空串，不支持的值抛出 E_SERIALIZE。
    std::string serialize(JSValueConst value);
    /// 排空任务并取得 Promise 结果；无任务可推进的 pending 状态使上下文失效。
    Value settle(Value value);
    /// 在同一截止时间内执行全部待处理任务，传播任务异常。
    void drain();
    /// 释放拒绝跟踪表的引用；须在上下文有效期间调用。
    void clearRejections() noexcept;
    /// 按全局脚本执行源码，使用 filename 标注堆栈，并处理返回的 Promise。
    Value evaluate(const std::string& source, const std::string& filename);
    /// 按别名、函数名、JSON 参数数组调用函数；空别名表示全局对象。
    Value call(const std::vector<std::string>& args);
    /// 动态导入根目录内模块，将其命名空间绑定到唯一非空别名。
    Value loadModule(const std::string& alias, const std::string& name);
    /// fs 原生入口；借助上下文宿主指针检查权限和配额，不向引擎抛 C++ 异常。
    static JSValue fileOperation(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int operation);
    /// 引擎中断回调；opaque 指向本实例，非零返回值终止 JS 执行。
    static int interrupt(JSRuntime*, void* opaque);
    /// 解析模块标识为受根目录约束的规范路径；返回字符串由引擎释放。
    static char* normalize(JSContext*, const char* base, const char* name, void* opaque);
    /// 读取并编译模块，定义由引擎上下文管理；失败设置 JS 异常并返回空指针。
    static JSModuleDef* load(JSContext*, const char* name, void* opaque);
    /// 跟踪拒绝及后续处理通知，维护 Promise/原因引用；不跨 C 回调抛异常。
    static void rejection(JSContext*, JSValueConst promise, JSValueConst reason, bool handled, void* opaque);
    /// 带配额和长度头部分配内存；失败设置 oom_ 并返回空指针。
    static void* allocate(void*, size_t);
    /// 检查乘法溢出后分配并清零，遵循相同的内存配额。
    static void* allocateZero(void*, size_t, size_t);
    /// 释放带头部的内存并扣减用量；允许空指针。
    static void deallocate(void*, void*);
    /// 调整分配大小；失败保留旧块，大小为零时释放旧块。
    static void* reallocate(void*, void*, size_t);
    /// 查询有效载荷大小；空指针返回零，不计入头部开销。
    static size_t allocationSize(const void*);
};
}
