# 第三方依赖

| 组件 | 固定版本/提交 | 用途 | 许可证 |
| --- | --- | --- | --- |
| [QuickJS-NG](https://github.com/quickjs-ng/quickjs) | v0.17.0 / `6d46d07d04041b40f4f49eaa7fdebe44c314c699` | 静态链接 JS 引擎 | MIT |
| [NSIS](https://github.com/kichik/nsis) | v3.13 / `a4ba28e60bd9e7c271cd2bfc1558889ce3355653` | SDK 头文件、测试安装器工具链 | SDK 为 zlib/libpng；其他部分见 COPYING |
| [zlib](https://github.com/madler/zlib) | v1.3.1 / `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` | 构建 NSIS 编译器 | zlib |
| [SCons](https://github.com/SCons/scons) | 4.8.1 | 构建 NSIS 工具链 | MIT |

`third_party/nsis` 中的 SDK 头文件从上述提交原样复制，许可证保存在该目录的 `COPYING`。
发行包包含 `licenses/QuickJS-NG.txt` 和 `licenses/NSIS.txt`。zlib、SCons 和 NSIS 编译器不随插件发行。

`src/quickjs_msvc_x86.h` 是本项目的 MSVC x86 适配：用两次 32 位位扫描实现 `_BitScanForward64`。
通过编译参数注入，不改写 QuickJS-NG 源文件。

`cmake/PatchQuickJs.cmake` 在构建目录生成 QuickJS-NG 修正版，修复待定异步模块销毁时的悬空引用：
上下文释放模块成员后放弃其引用，模块结构体延迟到最后一个持有者释放时回收。修改位置带有 `nsQuickJs` 标记，保留上游 MIT 声明。
回归用例包括普通待定模块、由全局对象持有 Promise 的待定模块，以及模块执行超时。
