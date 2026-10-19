#include "files.h"
#include "paths.h"
#include "runtime.h"
#include <windows.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace nsq {
namespace {
// 文本读写及追加后的文件大小上限，单位为 UTF-8 字节。
constexpr size_t maxFileBytes = 16 * 1024 * 1024;
// 单次目录枚举上限，避免宿主缓冲区不受控增长。
constexpr size_t maxDirectoryEntries = 4096;
// 方法表与分派共用的内部编号，不作为公共 ABI 暴露。
enum Operation { ReadText, WriteText, AppendText, Exists, Mkdir, ReadDir, Remove, Stat };
// 独占 Win32 文件句柄，在异常和正常退出路径均自动关闭。
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;

// 用路径分量进行 Windows 大小写不敏感比较，拒绝相似目录名前缀绕过。
bool within(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto child = path.begin();
    for (auto parent = root.begin(); parent != root.end(); ++parent, ++child) {
        if (child == path.end() || CompareStringOrdinal(parent->c_str(), -1, child->c_str(), -1, TRUE) != CSTR_EQUAL)
            return false;
    }
    return true;
}

// 不做隐式类型转换，避免参数的 toString 在原生文件操作中重入 JS。
std::string stringArgument(JSContext* ctx, JSValueConst value) {
    if (!JS_IsString(value)) throw Fault("E_ARGUMENT", "fs 参数必须是字符串");
    size_t size = 0;
    const char* data = JS_ToCStringLen(ctx, &size, value);
    if (!data) throw Fault("E_MEMORY", "无法读取 fs 字符串参数");
    try {
        std::string result(data, size);
        JS_FreeCString(ctx, data);
        return result;
    } catch (...) { JS_FreeCString(ctx, data); throw; }
}

// 拒绝 Win32 特殊文件名、备用数据流和尾随点/空格，避免路径解释产生歧义。
void validateName(const std::filesystem::path& input) {
    for (const auto& part : input.relative_path()) {
        auto name = part.wstring();
        if (name == L"." || name == L"..") continue;
        if (name.empty() || name.back() == L'.' || name.back() == L' ' ||
            name.find_first_of(L"<>:\"|?*") != std::wstring::npos ||
            std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32; }))
            throw Fault("E_PATH", "fs 路径包含非法文件名");
        auto stem = name.substr(0, name.find(L'.'));
        // 设备名只涉及 ASCII；同时拒绝 Windows 接受的上标数字设备名。
        for (auto& c : stem) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
            stem == L"CONIN$" || stem == L"CONOUT$" ||
            (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") &&
             ((stem[3] >= L'0' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3')))
            throw Fault("E_PATH", "fs 不支持 Windows 设备名");
    }
}

// 检查已存在分量的重解析点；不存在的尾部允许用于新建文件/目录。
void checkComponent(const std::filesystem::path& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
        throw Fault("E_IO", "无法访问 fs 路径");
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw Fault("E_PATH", "fs 不允许符号链接、联接点或其他重解析点");
}

// 解析 root 内的现存或待创建路径，不接受驱动器相对路径及设备路径。
std::filesystem::path filePath(const std::filesystem::path& root, const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos)
        throw Fault("E_PATH", "fs 路径为空或包含空字符");
    auto wide = utf16(name);
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    if (wide.rfind(L"\\\\?\\", 0) == 0 || wide.rfind(L"\\\\.\\", 0) == 0 || wide.rfind(L"\\??\\", 0) == 0)
        throw Fault("E_PATH", "fs 不支持设备路径");
    std::filesystem::path input(wide);
    if ((input.has_root_name() || input.has_root_directory()) && !input.is_absolute())
        throw Fault("E_PATH", "fs 路径必须相对 rootDir 或为完整绝对路径");
    validateName(input);
    auto path = (input.is_absolute() ? input : root / input).lexically_normal();
    // 去掉词法规范化保留的尾部分隔符，使 "." 与根目录比较保持一致。
    if (path.filename().empty() && path != path.root_path()) path = path.parent_path();
    if (!within(root, path)) throw Fault("E_PATH", "fs 路径超出 rootDir");
    checkComponent(root);
    auto current = root;
    auto part = path.begin();
    for (auto ignored = root.begin(); ignored != root.end(); ++ignored) ++part;
    for (; part != path.end(); ++part) { current /= *part; checkComponent(current); }
    return path;
}

// 检查打开后的真实路径，防止文件句柄指向校验目录以外的对象。
void checkHandlePath(HANDLE handle, const std::filesystem::path& root) {
    DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!size) throw Fault("E_IO", "无法解析文件句柄路径");
    std::wstring name(size, L'\0');
    DWORD length = GetFinalPathNameByHandleW(handle, name.data(), size, FILE_NAME_NORMALIZED);
    if (!length || length >= size) throw Fault("E_IO", "无法解析文件句柄路径");
    name.resize(length);
    if (name.rfind(L"\\\\?\\UNC\\", 0) == 0) name = L"\\\\" + name.substr(8);
    else if (name.rfind(L"\\\\?\\", 0) == 0) name.erase(0, 4);
    if (!within(root, std::filesystem::path(name))) throw Fault("E_PATH", "文件句柄超出 rootDir");
}

// 打开普通磁盘文件；写入前拒绝硬链接，且不提前截断已有内容。
Handle openFile(const std::filesystem::path& root, const std::filesystem::path& path, bool writing) {
    auto raw = CreateFileW(path.c_str(), writing ? GENERIC_WRITE : GENERIC_READ, FILE_SHARE_READ,
        nullptr, writing ? OPEN_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (raw == INVALID_HANDLE_VALUE) throw Fault("E_IO", "无法打开文件");
    Handle handle(raw, CloseHandle);
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(raw) != FILE_TYPE_DISK || !GetFileInformationByHandle(raw, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) throw Fault("E_IO", "fs 只支持普通文件");
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || (writing && info.nNumberOfLinks > 1))
        throw Fault("E_PATH", "fs 不允许重解析点或写入硬链接文件");
    checkHandlePath(raw, root);
    return handle;
}

// 读取全部 UTF-8 文本，保留 BOM、换行和嵌入空字符；大小和编码均严格校验。
std::string readText(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto handle = openFile(root, path, false);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.get(), &size) || size.QuadPart < 0 || size.QuadPart > maxFileBytes)
        throw Fault("E_IO", "文件超过 16 MiB 上限或无法读取大小");
    std::string result(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(handle.get(), result.data(), static_cast<DWORD>(result.size()), &read, nullptr) || read != result.size())
        throw Fault("E_IO", "文件读取失败");
    utf16(result);
    return result;
}

// 覆盖或追加 UTF-8 文本；不自动建父目录，检查通过后才截断或定位文件。
void writeText(const std::filesystem::path& root, const std::filesystem::path& path,
               const std::string& text, bool append) {
    if (text.size() > maxFileBytes) throw Fault("E_IO", "写入内容超过 16 MiB 上限");
    utf16(text);
    auto handle = openFile(root, path, true);
    LARGE_INTEGER size{}, offset{};
    if (!GetFileSizeEx(handle.get(), &size) || size.QuadPart < 0)
        throw Fault("E_IO", "无法读取文件大小");
    if (append && (size.QuadPart > maxFileBytes || text.size() > maxFileBytes - static_cast<size_t>(size.QuadPart)))
        throw Fault("E_IO", "追加后的文件超过 16 MiB 上限");
    if (!SetFilePointerEx(handle.get(), offset, nullptr, append ? FILE_END : FILE_BEGIN) ||
        (!append && !SetEndOfFile(handle.get()))) throw Fault("E_IO", "无法定位或截断文件");
    DWORD written = 0;
    if (!WriteFile(handle.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) || written != text.size())
        throw Fault("E_IO", "文件写入失败");
}
}

JSValue createFileApi(JSContext* ctx, JSCFunctionMagic* callback) {
    // 名称与参数数量按 Operation 顺序排列，供 JS 方法的 length 和严格参数校验使用。
    static const char* names[] = {"readText", "writeText", "appendText", "exists", "mkdir", "readDir", "remove", "stat"};
    Value object(ctx, JS_NewObject(ctx));
    if (JS_IsException(object)) return JS_EXCEPTION;
    for (int i = ReadText; i <= Stat; ++i) {
        JSValue function = JS_NewCFunctionMagic(ctx, callback, names[i],
            i == WriteText || i == AppendText ? 2 : 1, JS_CFUNC_generic_magic, i);
        if (JS_IsException(function)) return JS_EXCEPTION;
        if (JS_DefinePropertyValueStr(ctx, object, names[i], function, JS_PROP_ENUMERABLE) < 0) return JS_EXCEPTION;
    }
    return object.release();
}

JSValue throwFileError(JSContext* ctx, const char* code, const char* message) {
    Value error(ctx, JS_NewError(ctx));
    if (JS_IsException(error)) return JS_EXCEPTION;
    Value codeValue(ctx, JS_NewString(ctx, code));
    if (JS_IsException(codeValue)) return JS_EXCEPTION;
    if (JS_DefinePropertyValueStr(ctx, error, "code", codeValue.release(), JS_PROP_C_W_E) < 0) return JS_EXCEPTION;
    Value messageValue(ctx, JS_NewString(ctx, message));
    if (JS_IsException(messageValue)) return JS_EXCEPTION;
    if (JS_DefinePropertyValueStr(ctx, error, "message", messageValue.release(), JS_PROP_C_W_E) < 0) return JS_EXCEPTION;
    return JS_Throw(ctx, error.release());
}

JSValue executeFileOperation(JSContext* ctx, const std::filesystem::path& root,
                            int operation, int argc, JSValueConst* argv) {
    const int arity = operation == WriteText || operation == AppendText ? 2 : 1;
    if (argc != arity) throw Fault("E_ARGUMENT", "fs 参数数量错误");
    auto path = filePath(root, stringArgument(ctx, argv[0]));
    std::error_code error;
    switch (operation) {
    case ReadText: {
        auto text = readText(root, path);
        return JS_NewStringLen(ctx, text.data(), text.size());
    }
    case WriteText:
    case AppendText:
        writeText(root, path, stringArgument(ctx, argv[1]), operation == AppendText);
        return JS_UNDEFINED;
    case Exists: {
        bool exists = std::filesystem::exists(path, error);
        if (error) throw Fault("E_IO", "无法查询路径");
        return JS_NewBool(ctx, exists);
    }
    case Mkdir:
        // 仅创建一层；已存在的目录视为成功，不递归创建或覆盖文件。
        std::filesystem::create_directory(path, error);
        if (error) throw Fault("E_IO", "无法创建目录，请确认父目录已存在");
        return JS_UNDEFINED;
    case ReadDir: {
        std::vector<std::string> names;
        std::filesystem::directory_iterator it(path, error), end;
        if (error) throw Fault("E_IO", "无法读取目录");
        for (; it != end; it.increment(error)) {
            if (error) break;
            if (names.size() == maxDirectoryEntries) throw Fault("E_IO", "目录超过 4096 项上限");
            names.push_back(utf8(it->path().filename().wstring()));
        }
        if (error) throw Fault("E_IO", "目录读取失败");
        std::sort(names.begin(), names.end());
        Value array(ctx, JS_NewArray(ctx));
        if (JS_IsException(array)) return JS_EXCEPTION;
        for (uint32_t i = 0; i < names.size(); ++i) {
            Value name(ctx, JS_NewStringLen(ctx, names[i].data(), names[i].size()));
            if (JS_IsException(name) || JS_SetPropertyUint32(ctx, array, i, name.release()) < 0) return JS_EXCEPTION;
        }
        return array.release();
    }
    case Remove: {
        if (within(path, root)) throw Fault("E_PATH", "不能删除 rootDir");
        bool removed = std::filesystem::remove(path, error);
        if (error) throw Fault("E_IO", "删除失败，只能删除文件或空目录");
        return JS_NewBool(ctx, removed);
    }
    case Stat: {
        auto status = std::filesystem::status(path, error);
        if (error || !std::filesystem::exists(status)) throw Fault("E_IO", "无法查询文件信息");
        bool file = std::filesystem::is_regular_file(status);
        auto size = file ? std::filesystem::file_size(path, error) : 0;
        if (error) throw Fault("E_IO", "无法查询文件大小");
        Value info(ctx, JS_NewObject(ctx));
        if (JS_IsException(info)) return JS_EXCEPTION;
        if (JS_SetPropertyStr(ctx, info, "isFile", JS_NewBool(ctx, file)) < 0 ||
            JS_SetPropertyStr(ctx, info, "isDirectory", JS_NewBool(ctx, std::filesystem::is_directory(status))) < 0 ||
            JS_SetPropertyStr(ctx, info, "size", JS_NewFloat64(ctx, static_cast<double>(size))) < 0) return JS_EXCEPTION;
        return info.release();
    }
    default:
        throw Fault("E_ARGUMENT", "未知 fs 操作");
    }
}
}
