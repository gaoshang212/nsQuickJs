#include "paths.h"
#include "runtime.h"
#include <windows.h>
#include <fstream>
#include <limits>

namespace nsq {
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > INT_MAX) throw Fault("E_ENCODING", "文本过长");
    // 两次转换分别测量和写入；显式长度保留空字符并拒绝孤立代理项。
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw Fault("E_ENCODING", "无效的 UTF-16 文本");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring utf16(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() > INT_MAX) throw Fault("E_ENCODING", "文本过长");
    // 使用严格模式，禁止 Windows 用替代字符掩盖无效 UTF-8。
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw Fault("E_ENCODING", "无效的 UTF-8 文本");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::filesystem::path resolvePath(const std::filesystem::path& root,
                                 const std::filesystem::path& base,
                                 const std::string& name) {
    // 拒绝空字符及盘符之后的冒号，阻止 NTFS 备用数据流路径。
    if (name.empty() || name.find('\0') != std::string::npos || name.find(':', 2) != std::string::npos)
        throw Fault("E_PATH", "无效的脚本路径");
    auto input = std::filesystem::path(utf16(name));
    if (input.has_root_name() && !input.is_absolute())
        throw Fault("E_PATH", "不支持驱动器相对路径");
    std::error_code error;
    // 先解析 .. 和文件系统链接，再逐路径分量检查，避免字符串前缀误判。
    auto path = std::filesystem::canonical(input.is_absolute() ? input : base / input, error);
    if (error) throw Fault("E_IO", "无法访问脚本：" + name);
    auto a = root.begin();
    auto b = path.begin();
    for (; a != root.end(); ++a, ++b) {
        if (b == path.end() || CompareStringOrdinal(a->c_str(), -1, b->c_str(), -1, TRUE) != CSTR_EQUAL)
            throw Fault("E_PATH", "脚本路径超出根目录");
    }
    if (!std::filesystem::is_regular_file(path, error) || error)
        throw Fault("E_IO", "脚本不是普通文件");
    return path;
}

std::string readScript(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw Fault("E_IO", "无法读取脚本");
    auto length = stream.tellg();
    if (length < 0 || length > 16 * 1024 * 1024)
        throw Fault("E_IO", "脚本文件超过 16 MiB 上限");
    std::string source(static_cast<size_t>(length), '\0');
    stream.seekg(0);
    if (!source.empty() && !stream.read(source.data(), length))
        throw Fault("E_IO", "脚本读取失败");
    // BOM 只从文件开头移除；通过严格转换验证编码，仍返回原始 UTF-8 源码。
    if (source.compare(0, 3, "\xef\xbb\xbf") == 0) source.erase(0, 3);
    utf16(source);
    return source;
}
}
