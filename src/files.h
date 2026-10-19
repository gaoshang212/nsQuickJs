#pragma once
#include <quickjs.h>
#include <filesystem>

namespace nsq {
/// 创建只读 fs 方法表；callback 在每次调用时检查宿主权限及运行时限制。
/// 返回值由调用方接管；引擎分配失败返回 JS_EXCEPTION。
JSValue createFileApi(JSContext* ctx, JSCFunctionMagic* callback);
/// 执行同步文件操作；路径限制在 root 内，宿主错误以 Fault 抛出。
/// operation 来自 createFileApi 的内部编号；返回 JSValue 的引用交给调用方。
JSValue executeFileOperation(JSContext* ctx, const std::filesystem::path& root,
                            int operation, int argc, JSValueConst* argv);
/// 构造带 code/message 的 JS Error 并抛入引擎；失败保留引擎的内存异常。
JSValue throwFileError(JSContext* ctx, const char* code, const char* message);
}
