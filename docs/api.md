# 接口

All exported functions use the NSIS plugin ABI with Unicode strings. The plugin registers an NSIS callback via `RegisterPluginCallback` to remain loaded across calls and clean up the JavaScript context when NSIS unloads it.

```nsis
nsQuickJs::Call "main" "calculate" "[20,22]"
Pop $0 ; 状态
Pop $1 ; JSON 结果或错误文本
```

`.nsh` 宏名为 `NsQuickJs_接口名`，最后两个参数依次为状态变量、结果变量，两者必须不同。

## 方法

| 方法 | 输入 | 成功结果 |
| --- | --- | --- |
| `Init` | 配置 JSON 对象 | 空字符串 |
| `Eval` | 普通 JS 脚本字符串 | 脚本完成值的 JSON |
| `EvalFile` | UTF-8 文件路径 | 普通脚本完成值的 JSON |
| `LoadModule` | 非空别名、模块路径 | 空字符串 |
| `Call` | 别名、函数名、JSON 参数数组 | 函数结果的 JSON |
| `SetFileAccess` | 字符串 `true` 或 `false` | 空字符串 |
| `Destroy` | 无 | 空字符串 |

`Call` 的空别名表示从全局对象查找函数。使用顶层 `function` 或显式设置 `globalThis.fn`；顶层 `let` 和 `const` 不会成为全局对象属性。不解析 `obj.method` 形式的函数名，函数以 `undefined` 为 `this` 调用。最多支持 1024 个实参。

## 初始化配置

```json
{"rootDir":"C:/scripts","timeoutMs":5000,"memoryLimitMiB":64}
```

- `rootDir` 必填，必须是存在的绝对目录。
- `timeoutMs` 为 1–600000 的整数，默认 5000 毫秒。
- `memoryLimitMiB` 为 1–1024 的整数，默认 64 MiB，限制引擎的分配请求总量，不等于进程总内存。
- `allowFileAccess` 为 JSON 布尔值，默认 `false`；只有 `true` 才允许脚本调用 `fs`。字符串 `"true"` 等非布尔值返回 `E_ARGUMENT`。
- JS 原生栈限额固定为 256 KiB。单个脚本文件最大 16 MiB。
- 未识别的配置字段忽略。错误 JSON 返回 `E_JS`，字段错误返回 `E_ARGUMENT` 或 `E_PATH`。

一个进程仅维护一个上下文，必须在创建线程使用。重复初始化返回 `E_STATE`。显式销毁或 NSIS 卸载回调清理上下文；销毁可重复执行。

## 执行普通脚本文件：EvalFile

```nsis
; 前提：Init 已成功，calculate.js 已释放到 rootDir 内。
!insertmacro NsQuickJs_EvalFile 'calculate.js' $0 $1
; 必须先检查 $0：0 表示成功，$1 为 JSON 结果；失败时 $1 为错误说明。
```

宏参数为 `PATH STATUS RESULT`，状态变量和结果变量必须不同。也可直接调用 `nsQuickJs::EvalFile "calculate.js"`，随后依次弹出状态、结果。

- **文件准备**：插件只读取已有文件，不负责从安装器释放脚本。先用 NSIS 的 `SetOutPath`、`File` 准备文件，并将其所在根目录传给 `Init`。
- **路径**：相对路径基于 `rootDir`，也接受根目录内的绝对路径；解析 `..` 和文件系统链接后的路径不得越出根目录。路径指向普通文件，不自动补扩展名。
- **编码及大小**：文件内容须为 UTF-8，允许开头带 BOM；单个文件最大 16 MiB。只有路径经过 NSIS 参数栈，源码不受栈字符串长度限制，但返回结果仍受该限制。
- **执行及返回值**：按普通全局脚本执行，返回脚本完成值的 JSON。可将预期结果作为最后一个表达式，例如 `add(20, 22);` 返回 `42`；对象表达式应加括号，如 `({ total: 42 });`。不要使用顶层 `return`。完成值为 `undefined` 时返回空串；完成值为 Promise 时按任务队列规则等待。
- **状态复用**：与 `Eval`、`Call` 共用上下文，每次调用都重新读取并执行文件，不使用模块缓存。脚本中的全局函数可通过 `NsQuickJs_Call '' 'add' '[20,22]' $0 $1` 调用。重复执行含顶层 `let`/`const` 声明的脚本可能产生重复声明错误。
- **模块区别**：文件扩展名不会将 `EvalFile` 切换为模块模式；静态 `import`、`export` 和顶层 `await` 应使用 `LoadModule`。模块导出函数随后通过别名调用。

常见失败包括文件不存在或不可读时的 `E_IO`、路径越界时的 `E_PATH`、非法 UTF-8 的 `E_ENCODING`、脚本异常的 `E_JS`，以及结果过长的 `E_RESULT_TOO_LONG`。执行和序列化同样受超时、内存限制；传输结果失败不会回滚已执行的脚本。带脚本内容和错误检查的示例见 [README](../README.md#执行-js-文件nsquickjs_evalfile)。

## JS 文件访问：fs

`fs` 是全局对象，在 `Eval`、`EvalFile` 和 ES 模块中均可使用，不需要 `import`。它不是 Node.js 的 `fs`，所有方法同步执行，参数严格要求字符串，不进行隐式类型转换。方法和全局 `fs` 绑定不可覆盖；访问权限保存在宿主内部，不由 JS 属性控制。

### 由 NSIS 授权

默认禁止全部 `fs` 操作，包括存在性和属性查询。可在 `Init` 中设置 `"allowFileAccess":true`，或在上下文有效时调用：

```nsis
; 开启或撤销当前上下文的全部 fs 权限；$0 为状态，$1 为错误说明或空串。
!insertmacro NsQuickJs_SetFileAccess 'true' $0 $1
; 在这里检查 $0 并执行需要文件访问的脚本。
!insertmacro NsQuickJs_SetFileAccess 'false' $0 $1
```

`SetFileAccess` 只接受小写 `true`/`false`，否则返回 `E_ARGUMENT`；未初始化或失效时返回 `E_STATE`。每次文件操作都重新检查权限，因此缓存函数引用、模块和 Promise 回调也不能绕过撤销。JS 没有开启权限的方法。销毁后权限清除，下次初始化重新读取配置。此开关不影响宿主 `EvalFile`、`LoadModule` 或 JS 模块导入；它们仍遵循原有脚本加载边界。

### 方法与返回值

| JS 方法 | 行为及成功返回值 |
| --- | --- |
| `fs.readText(path)` | 读取 UTF-8 文本，返回字符串；保留 BOM、换行及嵌入空字符 |
| `fs.writeText(path, text)` | 新建或覆盖文件，不添加 BOM、不转换换行；返回 `undefined` |
| `fs.appendText(path, text)` | 追加文本，文件不存在则新建；返回 `undefined` |
| `fs.exists(path)` | 文件或目录存在返回 `true`，不存在返回 `false`；无权访问等错误仍抛异常 |
| `fs.mkdir(path)` | 创建单层目录；目录已存在时成功，父目录必须存在；返回 `undefined` |
| `fs.readDir(path)` | 返回直属条目的文件名数组（包含文件和目录），按 UTF-8 字节序排序，不递归 |
| `fs.remove(path)` | 删除普通文件或空目录；成功返回 `true`，不存在返回 `false`；不能删除根目录或递归删除 |
| `fs.stat(path)` | 返回 `{ isFile, isDirectory, size }`，`size` 为字节数；目录的 `size` 为 0 |

相对路径始终基于 `rootDir`，不是当前模块目录；根目录内的绝对路径也可使用。拒绝越界、空路径、空字符、驱动器相对路径、Windows 设备名、备用数据流及尾随点/空格。文件访问不允许经过符号链接、目录联接或其他重解析点；写入和追加还会拒绝已有硬链接文件。目录枚举可返回链接名称，但对这些链接进行访问会被拒绝。

读取文件、单次写入内容以及追加后的总长度均不得超过 16 MiB；目录枚举最多 4096 项。写入前须由调用者创建父目录。文本必须为有效 UTF-8，不提供二进制接口；若读取含 BOM 的 JSON，可先使用 `.replace(/^\uFEFF/, '')` 再 `JSON.parse`。

```js
// 宿主已授权后写入配置；这里仅操作 rootDir 内的文件。
fs.writeText('settings.json', JSON.stringify({ language: 'zh-CN' }));
try {
    // 读取后解析 JSON，文件接口本身只处理文本。
    JSON.parse(fs.readText('settings.json'));
} catch (error) {
    // 文件错误通过 code 区分，权限关闭时为 E_PERMISSION。
    ({ code: error.code, message: error.message });
}
```

### 错误和副作用

文件接口抛出带 `code` 和 `message` 的 JS `Error`：`E_PERMISSION` 表示未授权，`E_ARGUMENT` 表示参数类型或数量错误，`E_PATH` 表示路径被拒绝，`E_ENCODING` 表示文本编码无效，`E_IO` 表示读写、目录或大小限制错误。可在 JS 中捕获并继续执行；未捕获时，NSIS 收到外层 `E_JS`，而不是直接收到 `E_PERMISSION` 等内部错误码。运行时超时和内存耗尽仍优先按 `E_TIMEOUT`/`E_MEMORY` 处理，并使上下文失效。

操作不是事务：写入失败、后续 JS 异常、超时或返回值过长都不回滚文件副作用；磁盘写入失败可能留下部分内容。同步系统 I/O 不能被 JS 中断，返回引擎后才检查截止时间。宿主缓冲区不计入 QuickJS 的内存配额。此接口面向可信脚本，路径检查不是系统级沙箱，也不防御其他进程在操作期间替换目录树；不要将会被并发改动链接的目录授权给脚本。

## 模块和异步

入口允许根目录内的绝对路径或相对路径。依赖使用 `./`、`../`，必须包含扩展名；路径解析后不得越出根目录。UTF-8 文件允许 BOM。无扩展名自动补全、包名、URL 和原生模块均不支持。

模块按规范化路径缓存，同一路径可注册多个别名，但同一别名不可覆盖。模块状态持续到上下文销毁；文件修改不会热重载。循环依赖遵循 ECMAScript 规则，不能在初始化前读取尚未初始化的绑定。

函数返回 Promise 时等待其完成，同时处理任务队列；模块支持顶层 `await`。返回值或模块初始化仍待定且没有可执行任务时返回 `E_PENDING`。检测未处理的 Promise 拒绝。未被返回、没有后续任务的待定 Promise 不作为操作完成条件。

不提供外部事件循环、定时器和 I/O Promise。执行期限覆盖 JS 执行、任务队列及 JSON 序列化，不能打断 Windows 文件系统调用。

## 返回值及错误

结果按内置 `JSON.stringify` 语义序列化，不受脚本替换全局 `JSON.stringify` 影响。顶层 `undefined` 返回空字符串；JS 空字符串返回 `""`。非有限数按 JSON 规则返回 `null`；循环引用、BigInt 和顶层函数等不可序列化值返回错误。

输入容量由 NSIS 限制，插件无法恢复调用前已被 NSIS 截断的参数。输出按调用方实际容量检查，不写入截断的 JSON。错误信息允许截短，结尾标记 `...[截短]`。

| 状态 | 含义 |
| --- | --- |
| `0` | 成功 |
| `E_ARGUMENT` | 参数、别名或函数不正确 |
| `E_STATE` | 未初始化、重复初始化、上下文失效、并发或线程不匹配 |
| `E_JS` | 语法、运行时、模块导入错误或 Promise 拒绝 |
| `E_PATH` | 入口路径无效或越界 |
| `E_IO` | 入口文件不可读或超过大小限制 |
| `E_ENCODING` | UTF-8/UTF-16 编码无效 |
| `E_SERIALIZE` | 返回值无法转换为 JSON |
| `E_RESULT_TOO_LONG` | 输出超过 NSIS 字符串容量 |
| `E_TIMEOUT` | JS 执行超时 |
| `E_MEMORY` | 引擎内存上限或分配失败 |
| `E_PENDING` | 结果或模块初始化的 Promise 无法完成 |
| `E_INTERNAL` | 插件内部错误 |

模块依赖加载错误通过 JS 异常报告，因此外层状态为 `E_JS`。普通异常保留已有状态，不回滚副作用；若异常遗留任务队列则上下文失效，避免任务影响下一次调用。

`E_TIMEOUT`、`E_MEMORY`、`E_PENDING` 和运行时内部错误后，执行 `Destroy` 再 `Init`。`E_RESULT_TOO_LONG` 仅表示传输失败，脚本已执行且副作用保留。普通 `E_JS` 包含消息和可用的调用栈；抛出的非 Error 对象不保证提供调用栈。
