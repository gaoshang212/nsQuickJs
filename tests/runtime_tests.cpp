#include "runtime.h"
#include "paths.h"
#include "file_tests.h"
#include <iostream>
#include <fstream>
#include <windows.h>
#include <crtdbg.h>

// 接收夹具目录，运行运行时回归检查；参数错误返回 2，断言失败返回 1。
int main(int argc, char** argv) {
    // CI 中将断言和崩溃信息写到标准错误，避免弹窗阻塞自动测试。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    if (argc != 2) return 2;
    int failures = 0, checks = 0;
    nsq::Runtime rt;
    const auto root = nsq::utf8(std::filesystem::canonical(argv[1]).generic_wstring());
    const std::string config = "{\"rootDir\":\"" + root + "\",\"timeoutMs\":100,\"memoryLimitMiB\":8}";
    // 执行一次操作并比较错误码；checkText 为真时同时比较 JSON 或错误文本。
    // 返回原始结果，供文件名等非精确匹配的附加检查使用。
    auto expect = [&](const std::string& op, std::vector<std::string> args, const std::string& code,
                      const std::string& text = "", bool checkText = false) {
        ++checks;
        std::cout << "检查 " << checks << " " << op << "\n" << std::flush;
        auto result = rt.invoke(op, args);
        if (result.code != code || (checkText && result.text != text)) {
            ++failures;
            std::cerr << "失败 " << checks << " " << op << " 预期 " << code << ":" << text
                      << " 实际 " << result.code << ":" << result.text << "\n";
        }
        return result;
    };
    // 成功求值的简写，expected 是序列化后的 JSON，而非 JS 值本身。
    auto eval = [&](const std::string& source, const std::string& expected) {
        expect("Eval", {source}, "0", expected, true);
    };
    // 资源超限或 pending 后重建上下文，让后续用例不继承失效状态。
    auto reset = [&]() { expect("Destroy", {}, "0"); expect("Init", {config}, "0"); };
    // 生命周期和配置校验：失败的 Init 必须清理资源，成功后拒绝重复 Init。
    expect("Eval", {"1"}, "E_STATE");
    expect("Init", {"{}"}, "E_ARGUMENT");
    expect("Init", {"{\"rootDir\":\"relative\"}"}, "E_PATH");
    expect("Init", {config}, "0");
    expect("Init", {config}, "E_STATE");
    // 基本求值、Unicode、JSON 以及 undefined/NaN 的序列化约定。
    eval("1 + 2", "3");
    eval("'你好😀'", "\"你好😀\"");
    eval("({n:3, b:true, s:'a\\nb', a:[null,2]})", "{\"n\":3,\"b\":true,\"s\":\"a\\nb\",\"a\":[null,2]}");
    eval("undefined", ""); eval("null", "null");
    eval("NaN", "null");
    // 文件执行保留全局状态，并分别验证 BOM 与非法 UTF-8 夹具。
    expect("EvalFile", {"basic.js"}, "0", "1", true);
    expect("EvalFile", {"basic.js"}, "0", "2", true);
    expect("EvalFile", {"bom.js"}, "0", "\"带 BOM 的脚本\"", true);
    expect("EvalFile", {"invalid-utf8.js"}, "E_ENCODING");
    expect("Call", {"", "add", "[10,20]"}, "0", "30", true);
    expect("Call", {"", "add", "{}"}, "E_ARGUMENT");
    expect("Call", {"", "add", "["}, "E_JS");
    expect("Call", {"", "missing", "[]"}, "E_ARGUMENT");
    // 模块导入覆盖顶层 await、规范路径缓存、别名、循环依赖及中文文件名。
    expect("LoadModule", {"entry", "entry.mjs"}, "0");
    expect("Call", {"entry", "compute", "[4,5]"}, "0", "9", true);
    expect("Call", {"entry", "getReady", "[]"}, "0", "42", true);
    expect("LoadModule", {"math", "math.mjs"}, "0");
    expect("LoadModule", {"math2", "./math.mjs"}, "0");
    expect("Call", {"math2", "loads", "[]"}, "0", "1", true);
    expect("LoadModule", {"math", "math.mjs"}, "E_ARGUMENT");
    expect("Call", {"math", "echo", "[{\"value\":\"中文\"}]"}, "0", "{\"value\":\"中文\"}", true);
    auto error = expect("Call", {"math", "fail", "[]"}, "E_JS");
    if (error.text.find("math.mjs") == std::string::npos) { ++failures; std::cerr << "错误缺少文件名\n"; }
    expect("LoadModule", {"cycle", "cycle-a.mjs"}, "0");
    expect("Call", {"cycle", "both", "[]"}, "0", "\"ab\"", true);
    expect("LoadModule", {"cn", "中文.mjs"}, "0");
    expect("Call", {"cn", "greeting", "[]"}, "0", "\"你好😀\"", true);
    // 模块错误经 JS 异常返回；直接文件操作保留 E_PATH/E_IO 错误码。
    expect("LoadModule", {"missing", "missing.mjs"}, "E_JS");
    expect("LoadModule", {"bare", "bare.mjs"}, "E_JS");
    expect("LoadModule", {"escape", "escape.mjs"}, "E_JS");
    expect("EvalFile", {"../runtime_tests.cpp"}, "E_PATH");
    expect("EvalFile", {"none.js"}, "E_IO");
    expect("Eval", {"("}, "E_JS");
    expect("Eval", {"throw new Error('错误')"}, "E_JS");
    eval("counter", "2");
    // 返回 Promise 和旁支 Promise 都需排空；已处理拒绝不能误报。
    eval("Promise.resolve(7).then(x => x * 2)", "14");
    expect("Eval", {"Promise.reject('拒绝')"}, "E_JS");
    expect("Eval", {"Promise.reject('未处理'); 1"}, "E_JS");
    eval("Promise.reject('已处理').catch(() => 5)", "5");
    expect("LoadModule", {"reject", "rejected.mjs"}, "E_JS");
    // 不可序列化值应报错；普通异常和栈溢出后仍可继续执行。
    expect("Eval", {"1n"}, "E_SERIALIZE");
    expect("Eval", {"(() => {const a = {}; a.a=a; return a;})()"}, "E_SERIALIZE");
    expect("Eval", {"(() => {})"}, "E_SERIALIZE");
    expect("Eval", {"(function recur(){return recur()})()"}, "E_JS");
    eval("1", "1");
    // pending、忙循环、微任务循环、toJSON 和分配超限均需安全失效及销毁。
    expect("Eval", {"new Promise(() => {})"}, "E_PENDING");
    expect("Eval", {"1"}, "E_STATE");
    reset();
    expect("LoadModule", {"p", "pending.mjs"}, "E_PENDING");
    reset();
    expect("LoadModule", {"p", "pending-held.mjs"}, "E_PENDING");
    reset();
    expect("LoadModule", {"t", "timeout.mjs"}, "E_TIMEOUT");
    reset();
    expect("Eval", {"while(true){}"}, "E_TIMEOUT");
    expect("Eval", {"1"}, "E_STATE");
    reset();
    expect("Eval", {"({toJSON(){while(true){}}})"}, "E_TIMEOUT");
    reset();
    expect("Eval", {"function loop(){Promise.resolve().then(loop)}; loop()"}, "E_TIMEOUT");
    reset();
    expect("Eval", {"new ArrayBuffer(32 * 1024 * 1024)"}, "E_MEMORY");
    expect("Eval", {"1"}, "E_STATE");
    reset();
    expect("Eval", {"try { new ArrayBuffer(32 * 1024 * 1024) } catch(e) {}; 1"}, "E_MEMORY");
    reset();
    eval("typeof counter", "\"undefined\"");
    expect("Destroy", {}, "0"); expect("Destroy", {}, "0");
    std::cout << checks << " 项运行时检查，失败 " << failures << "\n";
    failures += testFileAccess();
    return failures ? 1 : 0;
}
