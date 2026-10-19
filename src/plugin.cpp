#include <windows.h>
#include <pluginapi.h>
#include "runtime.h"
#include "paths.h"
#include <atomic>
#include <cstring>
#include <memory>

namespace {
// DLL 实例句柄，用于向 NSIS 注册卸载回调。
HMODULE moduleHandle;
// 安装器进程共享的运行时；由 dispatch 串行访问并在卸载时销毁。
nsq::Runtime runtime;
// 调用期间的占用标记，同时拒绝并发调用与同线程重入。
std::atomic_flag busy = ATOMIC_FLAG_INIT;
// 成功 Init 的线程 ID；零表示尚未绑定或已 Destroy。
DWORD ownerThread = 0;

// NSIS 卸载通知：清理 JS 状态并解除线程绑定，返回值按 SDK 约定为零。
UINT_PTR __cdecl onPluginEvent(NSPIM event) {
    if (event == NSPIM_UNLOAD) { runtime.destroy(); ownerThread = 0; }
    return 0;
}

// 独占尚未压栈的节点；NSIS 栈使用 GlobalAlloc/GlobalFree 管理内存。
using Node = std::unique_ptr<stack_t, decltype(&GlobalFree)>;
// 创建容量为 capacity 个 UTF-16 码元的节点；调用者保证 value 含终止符可容纳。
// 分配失败抛出 bad_alloc，节点移交给 NSIS 栈后由宿主释放。
Node node(const std::wstring& value, int capacity) {
    auto ptr = static_cast<stack_t*>(GlobalAlloc(GPTR, offsetof(stack_t, text) + static_cast<size_t>(capacity) * sizeof(wchar_t)));
    if (!ptr) throw std::bad_alloc();
    Node result(ptr, GlobalFree);
    std::memcpy(ptr->text, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    return result;
}
// 压入结果和状态，使调用方依次 Pop 状态、结果；capacity 包含终止符且至少为 32。
// 成功数据超长时返回错误；错误消息允许截短，但不截断 UTF-16 代理项对。
void reply(stack_t** stack, int capacity, nsq::Result result) {
    std::wstring payload;
    try { payload = nsq::utf16(result.text); }
    catch (...) { result.code = "E_ENCODING"; payload = L"返回文本编码无效"; }
    if (payload.size() >= static_cast<size_t>(capacity)) {
        if (result.code == "0") { result.code = "E_RESULT_TOO_LONG"; payload = L"返回值超过 NSIS 字符串容量"; }
        if (payload.size() >= static_cast<size_t>(capacity)) {
            const std::wstring suffix = L"...[截短]";
            payload.resize(static_cast<size_t>(capacity) - 1 - suffix.size());
            if (!payload.empty() && payload.back() >= 0xd800 && payload.back() <= 0xdbff) payload.pop_back();
            payload += suffix;
        }
    }
    // 两个节点都分配成功后才修改栈，避免分配失败留下半份响应。
    auto data = node(payload, capacity);
    auto status = node(nsq::utf16(result.code), capacity);
    data->next = *stack;
    status->next = data.release();
    *stack = status.release();
}
// 统一 ABI 边界：消费 arity 个参数、检查线程与回调、分派操作并返回两个栈值。
// C++ 异常不得传播到 NSIS；无法构造正常响应时设置宿主 exec_error。
void dispatch(const char* operation, size_t arity, int capacity, stack_t** stack, extra_parameters* extra) noexcept {
    if (!stack || capacity < 32) { if (extra && extra->exec_flags) extra->exec_flags->exec_error = 1; return; }
    try {
        std::vector<std::string> args;
        args.reserve(arity);
        bool missing = false;
        nsq::Result parseError;
        // 即使某个参数编码错误也继续消费本次参数，保持下一次调用的栈位置。
        for (size_t i = 0; i < arity; ++i) {
            if (!*stack) { missing = true; break; }
            Node current(*stack, GlobalFree);
            *stack = current->next;
            try { args.push_back(nsq::utf8(current->text)); }
            catch (const nsq::Fault& e) { parseError = {e.code, e.what()}; }
        }
        if (missing) { reply(stack, capacity, {"E_ARGUMENT", "NSIS 栈参数不足"}); return; }
        if (parseError.code != "0") { reply(stack, capacity, parseError); return; }
        if (busy.test_and_set()) { reply(stack, capacity, {"E_STATE", "不支持并发或重入调用"}); return; }
        // 持有占用标记的作用域守卫，覆盖所有提前返回及异常路径。
        struct Unlock {
            // 退出 dispatch 时解除占用，允许后续调用。
            ~Unlock() { busy.clear(); }
        } unlock;
        if (ownerThread && ownerThread != GetCurrentThreadId()) {
            reply(stack, capacity, {"E_STATE", "上下文必须在创建线程调用"}); return;
        }
        if (!extra || !extra->RegisterPluginCallback || extra->RegisterPluginCallback(moduleHandle, onPluginEvent) < 0) {
            reply(stack, capacity, {"E_STATE", "无法注册 NSIS 退出回调"}); return;
        }
        auto result = runtime.invoke(operation, args);
        if (result.code == "0" && std::strcmp(operation, "Init") == 0) ownerThread = GetCurrentThreadId();
        if (result.code == "0" && std::strcmp(operation, "Destroy") == 0) ownerThread = 0;
        reply(stack, capacity, std::move(result));
    } catch (...) {
        if (extra && extra->exec_flags) extra->exec_flags->exec_error = 1;
        try { reply(stack, capacity, {"E_INTERNAL", "插件内部错误"}); } catch (...) {}
    }
}
}

// 在加载器锁内只记录模块并禁用线程通知；JS 清理由 NSIS 卸载回调负责。
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { moduleHandle = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
// 生成 NSIS 要求的 __cdecl 入口，name 为操作名，arity 为应消费的栈参数数。
// plugin.def 将内部 nsq_* 名称映射为稳定、无修饰的公共名称。
#define NSQ_EXPORT(name, arity) \
extern "C" void __cdecl nsq_##name(HWND, int size, wchar_t*, stack_t** stack, extra_parameters* extra) { \
    dispatch(#name, arity, size, stack, extra); \
}
// 以 JSON 配置创建共享上下文。
NSQ_EXPORT(Init, 1)
// 执行传入的 UTF-8 脚本源码。
NSQ_EXPORT(Eval, 1)
// 执行根目录内的脚本文件。
NSQ_EXPORT(EvalFile, 1)
// 按别名和路径加载 ES 模块。
NSQ_EXPORT(LoadModule, 2)
// 按别名、函数名和 JSON 参数数组调用函数。
NSQ_EXPORT(Call, 3)
// 由宿主传入 true/false，动态控制当前上下文的文件访问权限。
NSQ_EXPORT(SetFileAccess, 1)
// 幂等销毁共享上下文，不消费参数。
NSQ_EXPORT(Destroy, 0)
