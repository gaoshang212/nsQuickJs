#include <windows.h>
#include <pluginapi.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

namespace {
// 模拟标准 NSIS Unicode 字符串容量，包含结尾空字符。
constexpr int capacity = 1024;
// 模拟宿主参数栈；测试辅助函数负责释放未被插件消费的节点。
stack_t* stack = nullptr;
// 保存插件注册的卸载回调，供用例模拟 NSIS 退出。
NSISPLUGINCALLBACK callback = nullptr;
// 模拟 SDK 的注册入口；记录回调并以零表示成功。
int __stdcall registerCallback(HMODULE, NSISPLUGINCALLBACK value) { callback = value; return 0; }
// 按 NSIS 的内存布局压入 UTF-16 参数，非法测试输入直接终止测试。
void push(const std::wstring& value) {
    auto item = static_cast<stack_t*>(GlobalAlloc(GPTR, offsetof(stack_t, text) + capacity * sizeof(wchar_t)));
    if (!item || value.size() >= capacity) std::abort();
    std::memcpy(item->text, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    item->next = stack; stack = item;
}
// 弹出并释放一个节点；空栈返回诊断标记，让用例报告栈平衡错误。
std::wstring pop() {
    if (!stack) return L"<栈空>";
    auto item = stack; stack = stack->next;
    std::wstring result(item->text); GlobalFree(item); return result;
}
}
// 接收 DLL 路径和夹具目录，通过真实导出测试 ABI；非零退出码表示失败。
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    auto module = LoadLibraryW(argv[1]);
    if (!module) { std::cerr << "DLL 加载失败 " << GetLastError() << "\n"; return 1; }
    exec_flags_t flags{};
    extra_parameters extra{};
    extra.exec_flags = &flags; extra.RegisterPluginCallback = registerCallback;
    int failures = 0, checks = 0;
    // 逆序压入参数并调用导出；检查状态、可选返回值及栈底哨兵，随后清理残栈。
    auto call = [&](const char* op, std::vector<std::wstring> args, const std::wstring& code,
                    const std::wstring& result = L"", bool compare = false) {
        ++checks;
        // 与 NSIS Unicode 插件入口一致的函数签名，包括 x86 的调用约定。
        using Function = void(__cdecl*)(HWND, int, wchar_t*, stack_t**, extra_parameters*);
        auto function = reinterpret_cast<Function>(GetProcAddress(module, op));
        if (!function) { ++failures; return; }
        push(L"栈哨兵");
        for (auto it = args.rbegin(); it != args.rend(); ++it) push(*it);
        function(nullptr, capacity, nullptr, &stack, &extra);
        auto status = pop(), payload = pop(), sentinel = pop();
        if (status != code || (compare && payload != result) || sentinel != L"栈哨兵" || stack) {
            ++failures;
            std::wcerr << L"检查 " << checks << L" 失败，状态 " << status << L" 返回 " << payload << L"\n";
        }
        while (stack) pop();
    };
    const auto config = L"{\"rootDir\":\"" + std::filesystem::canonical(argv[2]).generic_wstring() + L"\"}";
    call("Init", {config}, L"0");
    call("Init", {config}, L"E_STATE");
    // 真实 DLL 导出必须支持宿主开关；关闭权限后仍允许正常求值。
    call("Eval", {L"(() => {try {fs.exists('basic.js')} catch(e) {return e.code}})()"}, L"0", L"\"E_PERMISSION\"", true);
    call("SetFileAccess", {L"true"}, L"0", L"", true);
    call("Eval", {L"fs.exists('basic.js')"}, L"0", L"true", true);
    call("SetFileAccess", {L"invalid"}, L"E_ARGUMENT");
    call("SetFileAccess", {L"false"}, L"0", L"", true);
    call("Eval", {L"globalThis.n = 4; n"}, L"0", L"4", true);
    call("Eval", {L"++n"}, L"0", L"5", true);
    call("Eval", {L"'你好😀'"}, L"0", L"\"你好😀\"", true);
    // JSON 引号也占容量：1021 个字符恰好容纳，增加一个字符必须报超长。
    call("Eval", {L"'x'.repeat(1021)"}, L"0", L"\"" + std::wstring(1021, L'x') + L"\"", true);
    call("Eval", {L"'x'.repeat(1022)"}, L"E_RESULT_TOO_LONG");
    call("Eval", {L"throw new Error('x'.repeat(5000))"}, L"E_JS");
    call("EvalFile", {L"basic.js"}, L"0", L"1", true);
    call("LoadModule", {L"math", L"math.mjs"}, L"0");
    call("Call", {L"math", L"add", L"[3,8]"}, L"0", L"11", true);
    call("Destroy", {}, L"0"); call("Destroy", {}, L"0");
    call("Init", {config}, L"0");
    call("LoadModule", {L"pending", L"pending-held.mjs"}, L"E_PENDING");
    // 保留 pending 模块触发卸载，验证清理顺序及重新初始化能力。
    if (!callback) ++failures;
    else callback(NSPIM_UNLOAD);
    call("Eval", {L"1"}, L"E_STATE");
    call("Init", {config}, L"0");
    call("Destroy", {}, L"0");
    FreeLibrary(module);
    std::cout << checks << " 项 DLL ABI 检查，失败 " << failures << "\n";
    return failures ? 1 : 0;
}
