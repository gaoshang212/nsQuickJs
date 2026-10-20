# nsQuickJs

[English](README.md) | **简体中文**

在 NSIS 安装器中执行 JavaScript 的开源插件，基于 QuickJS-NG，采用 MIT 许可证。

- 执行 JS 字符串和 UTF-8 脚本文件。
- 由 NSIS 授权后，通过 `fs` 读写 UTF-8 文件、管理目录；默认关闭。
- 保留上下文，复用变量、函数和模块状态。
- 加载 ES Modules，通过 JSON 调用导出函数。
- 支持可在 JS 任务队列内完成的 Promise 和顶层 `await`。
- 提供超时、内存限制和可检查的错误码。
- 同时构建 `x86-unicode` 和 `amd64-unicode` 插件。

## 下载

[Releases](https://github.com/gaoshang212/nsQuickJs/releases)

## 兼容性

| 组件 | 范围与验证情况 |
| --- | --- |
| 安装器 ABI | Unicode NSIS，提供 `x86-unicode` 和 `amd64-unicode`；不提供 ANSI 或原生 ARM64 插件 |
| NSIS | 已验证由固定 NSIS 3.13 源码构建的工具链；未验证旧版本 |
| Windows | 本地验证环境为系统构建号 10.0.26300；尚未确定最低支持的 Windows 版本 |
| 运行时依赖 | Release DLL 静态链接 QuickJS-NG 和 C 运行库，用户无需另行安装 |

本地测试覆盖两个架构的 Debug 和 Release。尚未在无开发工具的独立干净 Windows 系统上验证，具体结果及待完成检查见[验证记录](docs/validation.md)。

## 使用

发布包中的 `Plugins`、`Include` 和 `Examples` 应保持相对位置。普通 NSIS Unicode 安装器使用 x86 插件；原生 x64 安装器使用 amd64 插件。选择取决于安装器进程，而不是操作系统或被安装软件的位数。

```nsis
Unicode true
Target x86-unicode
!define NSQUICKJS_PLUGIN_DIR "path\to\nsQuickJs\Plugins"
!include "path\to\nsQuickJs\Include\nsQuickJs.nsh"

; 在 Section 中调用。JSON 路径使用正斜杠或转义反斜杠。
!insertmacro NsQuickJs_Init '{"rootDir":"C:/my-installer/scripts"}' $0 $1
; 每次检查 $0；只有 "0" 表示成功。
!insertmacro NsQuickJs_Eval '1 + 2' $0 $1
; $1 为 JSON 文本 3。
!insertmacro NsQuickJs_LoadModule 'main' 'main.mjs' $0 $1
!insertmacro NsQuickJs_Call 'main' 'calculate' '[20,22]' $0 $1
!insertmacro NsQuickJs_Destroy $0 $1
```

完整的错误检查与脚本释放示例见 [examples/demo.nsi](examples/demo.nsi)。模块示例：

```js
// 返回 Promise，完成后由插件将结果序列化为 JSON 传给 NSIS。
export async function calculate(a, b) {
    return { total: await Promise.resolve(a + b) };
}
```

改用 x64 时设置 `Target amd64-unicode`，并使用包含 amd64 安装器模板的 NSIS。项目提供从固定 NSIS 源码构建测试工具链的脚本。

### 执行 JS 文件：NsQuickJs_EvalFile

脚本较长时，可保存为 UTF-8 文件，通过 `NsQuickJs_EvalFile` 执行。例如将以下内容保存为 `calculate.js`：

```js
// 定义全局函数，后续也可通过 NsQuickJs_Call 的空别名调用。
function add(a, b) {
    return a + b;
}
// 最后一个表达式作为脚本结果，返回给 NSIS 的 JSON 文本为 42。
add(20, 22);
```

运行时文件必须已存在于 `rootDir` 内。使用前面示例的配置时，文件位置为 `C:/my-installer/scripts/calculate.js`；若随安装器分发，先通过 NSIS 的 `SetOutPath`、`File` 将脚本释放到目标目录，再以该目录初始化 `rootDir`。

在 `Init` 成功后、`Destroy` 之前，将以下代码放入 `Section`：

```nsis
; 路径相对于 rootDir，不是当前工作目录；$0/$1 分别接收状态和结果。
!insertmacro NsQuickJs_EvalFile 'calculate.js' $0 $1
StrCmp $0 '0' eval_file_ok
  DetailPrint "脚本执行失败：$0 / $1"
  SetErrorLevel 1
  Abort
eval_file_ok:
  DetailPrint "文件执行结果：$1" ; 此例输出 42。
```

文件允许 UTF-8 BOM，最大 16 MiB。每次调用都会重新读取并执行文件，变量和函数保留在共享上下文中。普通脚本使用 `EvalFile`；包含静态 `import`、`export` 或顶层 `await` 的 ES 模块使用 `NsQuickJs_LoadModule`。完整参数、路径和返回值规则见 [EvalFile 接口说明（英文）](docs/api.md#execute-script-files-evalfile)。

### JS 文件访问与权限开关

JS 全局对象 `fs` 提供同步文件操作，默认禁止调用。初始化时可用 JSON 布尔值开启：

```nsis
; rootDir 必须已存在；此配置允许当前上下文访问该目录内的文件。
!insertmacro NsQuickJs_Init '{"rootDir":"C:/my-installer/scripts","allowFileAccess":true}' $0 $1
; 检查 $0 为 0 后再执行脚本。
```

也可在初始化成功后，由 NSIS 动态开启或关闭：

```nsis
; ENABLED 只接受 true 或 false；每次调用后检查 $0。
!insertmacro NsQuickJs_SetFileAccess 'true' $0 $1
!insertmacro NsQuickJs_EvalFile 'files.js' $0 $1
; 撤销权限后，即使 JS 保存了 fs 方法引用也不能继续访问文件。
!insertmacro NsQuickJs_SetFileAccess 'false' $0 $1
```

`files.js` 中可以写入并读取 JSON：

```js
// 路径相对于 rootDir；JSON 序列化由脚本负责。
fs.writeText('settings.json', JSON.stringify({ language: 'zh-CN' }));
// 最后一个表达式作为 EvalFile 的返回结果。
JSON.parse(fs.readText('settings.json'));
```

还支持 `appendText`、`exists`、`mkdir`、`readDir`、`remove` 和 `stat`。权限只能由宿主修改，JS 无法自行开启；关闭文件访问不会禁用 `EvalFile` 或模块加载。文件操作限制在 `rootDir` 内，文本文件上限为 16 MiB，不支持二进制读写或 Node.js 的 `fs` API。完整方法及错误约定见 [文件访问接口（英文）](docs/api.md#javascript-file-access-fs)；[demo](examples/demo.nsi) 演示了授权、文件执行和撤销。

## 构建和测试

需要 Visual Studio 2022 的 C++ 桌面开发组件、Windows SDK、CMake 3.24+ 和 Git。测试安装器工具链另需 Python 3.8+。

```powershell
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release
cmake --preset x64
cmake --build --preset x64-release
ctest --preset x64-release

./scripts/bootstrap-nsis.ps1
./scripts/test-installer.ps1 -Arch x86
./scripts/test-installer.ps1 -Arch x64

cmake --install build/x86 --config Release --prefix dist/nsQuickJs
cmake --install build/x64 --config Release --prefix dist/nsQuickJs
```

以上命令在源码根目录执行。首次配置下载固定提交的 QuickJS-NG 源码包并验证 SHA-256。两个架构安装到同一目录，得到完整双架构包。
详细说明见 [构建指南（英文）](docs/build.md) 和 [接口文档（英文）](docs/api.md)。

## 边界

首版面向可信、短时的数据处理脚本。同步调用会阻塞安装器线程；不提供 Node.js、npm、网络、定时器、注册表或进程 API。文件访问需由宿主显式授权。

NSIS 栈参数通常只有 1024 个 UTF-16 单元的容量，包含末尾空字符。大脚本使用 `EvalFile`；结果超长返回 `E_RESULT_TOO_LONG`。不要将任意用户输入拼接进 `Eval`，应通过 `Call` 的 JSON 数组传参。

路径限制不构成不可信代码的系统级隔离。文件操作期间不应由其他进程并发改动目录链接；授权目录内已有文件可被脚本覆盖或删除。JS 执行期限无法中断 Windows 的阻塞文件 I/O；脚本和数据应放在本地目录。
