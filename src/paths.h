#pragma once
#include <filesystem>
#include <string>

namespace nsq {
/// 严格将 UTF-16 转为 UTF-8，保留嵌入空字符；非法编码抛出 E_ENCODING。
std::string utf8(const std::wstring& value);
/// 严格将 UTF-8 转为 UTF-16，保留嵌入空字符；非法编码抛出 E_ENCODING。
std::wstring utf16(const std::string& value);
/// 相对 base 解析 UTF-8 路径并跟随链接；结果须为规范化 root 内的普通文件。
/// root/base 由调用方提供；越界抛出 E_PATH，不存在或不可访问抛出 E_IO。
std::filesystem::path resolvePath(const std::filesystem::path& root,
                                  const std::filesystem::path& base,
                                  const std::string& name);
/// 读取不超过 16 MiB 的 UTF-8 脚本，去除可选 BOM；编码或 I/O 错误抛出 Fault。
std::string readScript(const std::filesystem::path& path);
}
