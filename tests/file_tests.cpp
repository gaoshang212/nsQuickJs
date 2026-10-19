#include "file_tests.h"
#include "runtime.h"
#include "paths.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
// 每个测试进程独占临时树，根目录及外部哨兵都不接触真实用户文件。
struct Fixture {
    // 本测试创建并拥有的临时父目录；只有这一棵树允许自动删除。
    std::filesystem::path base;
    // 授权给 JS 的目录。
    std::filesystem::path root;
    // 与 root 名称前缀相同的兄弟目录，用于验证前缀绕过和链接逃逸。
    std::filesystem::path outside;
    // 创建唯一目录后初始化夹具；Windows 进程 ID 防止并发测试互相覆盖。
    Fixture() {
        auto temp = std::filesystem::canonical(std::filesystem::temp_directory_path());
        base = temp / (L"nsQuickJs-files-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        if (!std::filesystem::create_directory(base)) throw std::runtime_error("无法创建独立文件测试目录");
        root = base / L"root";
        outside = base / L"root-sibling";
        std::filesystem::create_directory(root);
        std::filesystem::create_directory(outside);
        std::ofstream(outside / L"keep.txt", std::ios::binary) << "unchanged";
        std::ofstream(root / L"invalid.txt", std::ios::binary).put(static_cast<char>(0xff));
        std::ofstream(root / L"bom.txt", std::ios::binary) << "\xef\xbb\xbfhello";
        std::ofstream(root / L"normal.js", std::ios::binary) << "// 验证 fs 禁用时仍可执行脚本。\n42;\n";
        std::ofstream(root / L"large.txt", std::ios::binary).seekp(16 * 1024 * 1024).put('x');
        std::ofstream(root / L"file-module.mjs", std::ios::binary)
            << "// 模块函数同样使用宿主 fs 权限。\nexport function read() { return fs.readText('bom.txt'); }\n";
    }
    // 只回收本实例成功创建的绝对临时子目录，不跟随目录符号链接。
    ~Fixture() {
        std::error_code error;
        auto temp = std::filesystem::canonical(std::filesystem::temp_directory_path(), error);
        if (!error && base.parent_path() == temp && base.filename().wstring().rfind(L"nsQuickJs-files-", 0) == 0)
            std::filesystem::remove_all(base, error);
    }
};
}

int testFileAccess() {
    Fixture files;
    nsq::Runtime runtime;
    int checks = 0, failures = 0;
    const auto root = nsq::utf8(files.root.generic_wstring());
    // 临时路径来自系统目录，使用通用分隔符；配置给边界检查预留充足执行时间。
    const std::string config = "{\"rootDir\":\"" + root + "\",\"timeoutMs\":5000}";
    // 比较宿主错误码及可选 JSON，报告原始错误方便定位文件系统差异。
    auto expect = [&](const std::string& operation, std::vector<std::string> args,
                      const std::string& code, const std::string& expected = "", bool compare = true) {
        ++checks;
        auto result = runtime.invoke(operation, args);
        if (result.code != code || (compare && result.text != expected)) {
            ++failures;
            std::cerr << "文件检查 " << checks << " " << operation << " 失败：" << result.code << " " << result.text << "\n";
        }
    };
    // 对成功求值的结果做精确 JSON 比较。
    auto eval = [&](const std::string& source, const std::string& expected) {
        expect("Eval", {source}, "0", expected);
    };
    // 捕获文件接口的 Error.code，确认错误可由 JS 处理且不破坏上下文。
    auto error = [&](const std::string& source, const std::string& code) {
        eval("(() => { try { " + source + "; return 'NO_ERROR'; } catch(e) { return e.code; } })()", "\"" + code + "\"");
    };
    expect("SetFileAccess", {"true"}, "E_STATE", "", false);
    expect("Init", {config.substr(0, config.size() - 1) + ",\"allowFileAccess\":\"true\"}"}, "E_ARGUMENT", "", false);
    expect("Init", {config}, "0");
    // 默认关闭，包括 exists 也不能泄漏文件信息；保存的原生引用仍受实时权限检查。
    error("fs.exists('bom.txt')", "E_PERMISSION");
    eval("globalThis.savedRead = fs.readText; undefined", "");
    expect("SetFileAccess", {"1"}, "E_ARGUMENT", "", false);
    error("savedRead('bom.txt')", "E_PERMISSION");
    expect("SetFileAccess", {"true"}, "0");
    eval("fs.mkdir('中文'); fs.writeText('中文/你好.txt', '你好😀\\nA\\u0000B'); fs.readText('中文/你好.txt')", "\"你好😀\\nA\\u0000B\"");
    eval("fs.stat('中文/你好.txt')", "{\"isFile\":true,\"isDirectory\":false,\"size\":14}");
    eval("fs.appendText('中文/你好.txt', '尾'); fs.readText('中文/你好.txt')", "\"你好😀\\nA\\u0000B尾\"");
    eval("fs.writeText('中文/你好.txt', 'short'); fs.readText('中文/你好.txt')", "\"short\"");
    eval("fs.readText('" + root + "/中文/你好.txt')", "\"short\"");
    eval("fs.writeText('中文/empty.txt', ''); fs.readText('中文/empty.txt')", "\"\"");
    eval("fs.appendText('中文/new.txt', 'new'); fs.readText('中文/new.txt')", "\"new\"");
    eval("fs.readDir('中文')", "[\"empty.txt\",\"new.txt\",\"你好.txt\"]");
    eval("fs.stat('中文')", "{\"isFile\":false,\"isDirectory\":true,\"size\":0}");
    eval("fs.exists('missing')", "false");
    eval("fs.exists('.')", "true");
    eval("fs.mkdir('中文')", "");
    eval("savedRead('bom.txt')", "\"\ufeffhello\"");
    error("fs.readText('missing')", "E_IO");
    error("fs.readText('invalid.txt')", "E_ENCODING");
    error("fs.readText('large.txt')", "E_IO");
    error("fs.appendText('large.txt', 'a')", "E_IO");
    error("fs.writeText('bom.txt', 'x'.repeat(16 * 1024 * 1024 + 1))", "E_IO");
    error("fs.writeText('bom.txt', '\\ud800')", "E_ENCODING");
    eval("fs.readText('bom.txt')", "\"\ufeffhello\"");
    error("fs.readText('中文')", "E_IO");
    error("fs.readDir('bom.txt')", "E_IO");
    error("fs.writeText('missing/file.txt', 'x')", "E_IO");
    error("fs.mkdir('missing/child')", "E_IO");
    error("fs.mkdir('bom.txt')", "E_IO");
    error("fs.remove('中文')", "E_IO");
    error("fs.remove('.')", "E_PATH");
    error("fs.remove('中文/..')", "E_PATH");
    error("fs.readText('../root-sibling/keep.txt')", "E_PATH");
    error("fs.writeText('../root-sibling/new.txt', 'x')", "E_PATH");
    error("fs.remove('../root-sibling/keep.txt')", "E_PATH");
    error("fs.exists('" + nsq::utf8(files.outside.generic_wstring()) + "/keep.txt')", "E_PATH");
    for (const auto* path : {"bom.txt:stream", "NUL", "CON.txt", "file.", "file ", "C:relative", "/rooted", "bad\\u0000name", ""})
        error("fs.writeText('" + std::string(path) + "', 'x')", "E_PATH");
    error("fs.readText()", "E_ARGUMENT");
    error("fs.readText('bom.txt', 'utf8')", "E_ARGUMENT");
    error("fs.readText({toString(){throw new Error('coercion')}})", "E_ARGUMENT");
    error("fs.writeText('bom.txt', 1)", "E_ARGUMENT");
    // 硬链接不能用于覆盖根外内容；读取本来就存在于授权目录的硬链接仍允许。
    if (CreateHardLinkW((files.root / L"hard.txt").c_str(), (files.outside / L"keep.txt").c_str(), nullptr)) {
        error("fs.writeText('hard.txt', 'changed')", "E_PATH");
        error("fs.appendText('hard.txt', 'changed')", "E_PATH");
        eval("fs.remove('hard.txt')", "true");
    } else { ++failures; std::cerr << "无法创建硬链接测试夹具\n"; }
    // 开发者模式或提升权限允许符号链接时，覆盖越界联接和悬空链接场景。
    if (CreateSymbolicLinkW((files.root / L"link").c_str(), files.outside.c_str(), 0x1 | 0x2)) {
        error("fs.readText('link/keep.txt')", "E_PATH");
        error("fs.writeText('link/new.txt', 'x')", "E_PATH");
        error("fs.exists('link')", "E_PATH");
        error("fs.remove('link')", "E_PATH");
    } else std::cout << "文件检查：当前系统无符号链接创建权限，跳过目录链接用例\n";
    if (CreateSymbolicLinkW((files.root / L"dangling").c_str(), (files.outside / L"missing.txt").c_str(), 0x2))
        error("fs.writeText('dangling', 'x')", "E_PATH");
    else std::cout << "文件检查：当前系统无符号链接创建权限，跳过悬空链接用例\n";
    expect("LoadModule", {"files", "file-module.mjs"}, "0");
    expect("Call", {"files", "read", "[]"}, "0", "\"\ufeffhello\"");
    expect("SetFileAccess", {"false"}, "0");
    error("savedRead('bom.txt')", "E_PERMISSION");
    error("fs.writeText('disabled.txt', 'x')", "E_PERMISSION");
    expect("Call", {"files", "read", "[]"}, "E_JS", "", false);
    // 文件访问禁用后，宿主仍能加载脚本；销毁及默认重建不能保留此前授权。
    expect("EvalFile", {"normal.js"}, "0", "42");
    expect("Destroy", {}, "0");
    expect("Init", {config}, "0");
    error("fs.exists('bom.txt')", "E_PERMISSION");
    expect("Destroy", {}, "0");
    expect("Init", {config.substr(0, config.size() - 1) + ",\"allowFileAccess\":true}"}, "0");
    eval("fs.remove('中文/你好.txt')", "true");
    eval("fs.remove('中文/你好.txt')", "false");
    eval("fs.remove('中文/empty.txt'); fs.remove('中文/new.txt'); fs.remove('中文')", "true");
    expect("Destroy", {}, "0");
    // 在 JS 调用之外确认拒绝写入没有改变哨兵，也没有意外创建文件。
    std::string sentinel;
    std::ifstream(files.outside / L"keep.txt") >> sentinel;
    if (sentinel != "unchanged" || std::filesystem::exists(files.outside / L"new.txt") ||
        std::filesystem::exists(files.outside / L"missing.txt") || std::filesystem::exists(files.root / L"disabled.txt")) ++failures;
    std::cout << checks << " 项文件访问检查，失败 " << failures << "\n";
    return failures;
}
