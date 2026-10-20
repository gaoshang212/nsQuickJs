# API Reference

All exported functions use the NSIS plugin ABI with Unicode strings. The plugin registers an NSIS callback via `RegisterPluginCallback` to remain loaded across calls and clean up the JavaScript context when NSIS unloads it.

```nsis
nsQuickJs::Call "main" "calculate" "[20,22]"
Pop $0 ; Status code.
Pop $1 ; JSON result or error message.
```

The `.nsh` macros use the naming pattern `NsQuickJs_<Method>`. Their final two arguments are the status variable and result variable, in that order. These must be different variables.

## Methods

| Method | Input | Successful result |
| --- | --- | --- |
| `Init` | JSON configuration object | Empty string |
| `Eval` | Ordinary JavaScript source string | Script completion value as JSON |
| `EvalFile` | Path to a UTF-8 script file | Script completion value as JSON |
| `LoadModule` | Nonempty alias, module path | Empty string |
| `Call` | Alias, function name, JSON argument array | Function result as JSON |
| `SetFileAccess` | String `true` or `false` | Empty string |
| `Destroy` | None | Empty string |

An empty alias in `Call` looks up the function on the global object. Define it with a top-level `function` declaration or explicitly assign `globalThis.fn`; top-level `let` and `const` bindings are not global object properties. Names such as `obj.method` are not resolved as property paths. Functions are called with `undefined` as `this`. Calls accept at most 1024 arguments.

## Initialization

```json
{"rootDir":"C:/scripts","timeoutMs":5000,"memoryLimitMiB":64}
```

- `rootDir` is required and must be an existing absolute directory path.
- `timeoutMs` is an integer from 1 to 600000, with a default of 5000 milliseconds.
- `memoryLimitMiB` is an integer from 1 to 1024, with a default of 64 MiB. It limits allocations requested by the engine, not total process memory.
- `allowFileAccess` is a JSON boolean and defaults to `false`. Only `true` permits scripts to call `fs`. Non-boolean values, including the string `"true"`, return `E_ARGUMENT`.
- The JavaScript native stack limit is fixed at 256 KiB. Each script file is limited to 16 MiB.
- Unknown configuration fields are ignored. Invalid JSON returns `E_JS`; invalid field values return `E_ARGUMENT` or `E_PATH`.

Each process has one context, which must be used on the thread that created it. Repeated initialization returns `E_STATE`. Explicit destruction or the NSIS unload callback cleans up the context. Repeated destruction is allowed.

## Execute script files: EvalFile

```nsis
; Prerequisites: Init succeeded and calculate.js has been extracted inside rootDir.
!insertmacro NsQuickJs_EvalFile 'calculate.js' $0 $1
; Check $0 first: 0 means success and $1 contains JSON; on failure, $1 contains an error message.
```

The macro arguments are `PATH STATUS RESULT`; the status and result variables must differ. You can also call `nsQuickJs::EvalFile "calculate.js"` directly, then pop the status and result in that order.

- **File preparation:** The plugin reads existing files; it does not extract scripts from the installer. Prepare them with NSIS `SetOutPath` and `File`, then pass their root directory to `Init`.
- **Paths:** Relative paths are resolved against `rootDir`. Absolute paths inside that directory are also accepted. After resolving `..` and filesystem links, the path must remain inside the root directory. It must identify a regular file; extensions are not added automatically.
- **Encoding and size:** Files must contain UTF-8 text, optionally starting with a BOM, and must not exceed 16 MiB. Only the path passes through the NSIS argument stack, so the source is not subject to the stack string limit. The returned result is still subject to that limit.
- **Execution and results:** Files execute as ordinary global scripts and return their completion value as JSON. Use a final expression for the desired result: for example, `add(20, 22);` returns `42`. Wrap object expressions in parentheses, as in `({ total: 42 });`. Do not use top-level `return`. An `undefined` completion value produces an empty string; a Promise completion value is awaited according to the job queue rules.
- **Shared state:** Execution shares the context used by `Eval` and `Call`. Each invocation reads and executes the file again, without module caching. Global functions defined by the script can be called with `NsQuickJs_Call '' 'add' '[20,22]' $0 $1`. Repeating a script with top-level `let` or `const` declarations may cause redeclaration errors.
- **Module distinction:** The file extension does not switch `EvalFile` into module mode. Use `LoadModule` for static `import`, `export`, and top-level `await`, then call exported functions through the module alias.

Common failures include `E_IO` for missing or unreadable files, `E_PATH` for paths outside the root, `E_ENCODING` for invalid UTF-8, `E_JS` for script exceptions, and `E_RESULT_TOO_LONG` for oversized results. Execution and serialization are subject to the configured time and memory limits. A result transmission failure does not roll back script execution. See the [README example](../README.md#execute-a-script-file-nsquickjs_evalfile) for script contents and error handling.

## JavaScript file access: fs

`fs` is a global object available in `Eval`, `EvalFile`, and ES modules without an `import`. It is not Node.js's `fs`. All methods execute synchronously and require string arguments without implicit conversion. The methods and global `fs` binding cannot be overwritten. Permissions are stored in the host, not controlled by JavaScript properties.

### Host authorization

All `fs` operations are disabled by default, including existence and metadata queries. Set `"allowFileAccess":true` in `Init`, or use the following calls while the context is valid:

```nsis
; Grant or revoke all fs permissions for the current context.
; $0 receives the status; $1 receives an error message or an empty string.
!insertmacro NsQuickJs_SetFileAccess 'true' $0 $1
; Check $0 here, then run scripts that need file access.
!insertmacro NsQuickJs_SetFileAccess 'false' $0 $1
```

`SetFileAccess` accepts only lowercase `true` or `false`; other values return `E_ARGUMENT`. An uninitialized or invalid context returns `E_STATE`. Permissions are checked on every file operation, so saved function references, modules, and Promise callbacks cannot bypass revocation. JavaScript has no method to enable access. Destruction clears the permission, and the next initialization reads its own configuration. This switch does not affect host calls to `EvalFile` or `LoadModule`, or JavaScript module imports; their existing script-loading boundaries still apply.

### File methods and return values

| JavaScript method | Behavior and successful return value |
| --- | --- |
| `fs.readText(path)` | Reads UTF-8 text and returns a string, preserving the BOM, line endings, and embedded null characters |
| `fs.writeText(path, text)` | Creates or overwrites a file without adding a BOM or changing line endings; returns `undefined` |
| `fs.appendText(path, text)` | Appends text, creating the file if it does not exist; returns `undefined` |
| `fs.exists(path)` | Returns `true` for an existing file or directory and `false` if absent; errors such as access denial still throw |
| `fs.mkdir(path)` | Creates one directory level; succeeds if the directory already exists; the parent must exist; returns `undefined` |
| `fs.readDir(path)` | Returns the names of immediate entries, including files and directories, sorted by UTF-8 byte order; does not recurse |
| `fs.remove(path)` | Deletes a regular file or empty directory; returns `true` if removed and `false` if absent; cannot delete the root or recurse |
| `fs.stat(path)` | Returns `{ isFile, isDirectory, size }`; `size` is in bytes and is 0 for directories |

Relative paths always start at `rootDir`, not the current module's directory. Absolute paths inside the root are also accepted. Paths outside the root, empty paths, null characters, drive-relative paths, Windows device names, alternate data streams, and trailing dots or spaces are rejected. File operations cannot traverse symbolic links, directory junctions, or other reparse points. Writes and appends also reject existing hard-linked files. Directory enumeration may return link names, but accessing those links is rejected.

Files read, individual write payloads, and total file lengths after appending must not exceed 16 MiB. Directory enumeration is limited to 4096 entries. Callers must create parent directories before writing. Text must be valid UTF-8; no binary interface is provided. When reading JSON with a BOM, use `.replace(/^\uFEFF/, '')` before `JSON.parse` if needed.

```js
// After host authorization, write configuration inside rootDir.
fs.writeText('settings.json', JSON.stringify({ language: 'zh-CN' }));
try {
    // Parse JSON after reading; the file API itself handles text only.
    JSON.parse(fs.readText('settings.json'));
} catch (error) {
    // File errors have a code; disabled access produces E_PERMISSION.
    ({ code: error.code, message: error.message });
}
```

### File errors and side effects

The file API throws JavaScript `Error` objects with `code` and `message` properties. `E_PERMISSION` means access is disabled, `E_ARGUMENT` indicates an incorrect argument type or count, `E_PATH` indicates a rejected path, `E_ENCODING` indicates invalid text encoding, and `E_IO` covers file operations, directory operations, and size limits. Scripts can catch these errors and continue. If an error is uncaught, NSIS receives the outer `E_JS` status rather than the internal code such as `E_PERMISSION`. Runtime timeout and memory exhaustion still take precedence as `E_TIMEOUT` or `E_MEMORY` and invalidate the context.

Operations are not transactional. Write failures, later JavaScript exceptions, timeouts, and oversized return values do not roll back filesystem changes. A failed disk write may leave partial content. JavaScript cannot interrupt synchronous system I/O; the deadline is checked after I/O returns to the engine. Host buffers do not count toward the QuickJS memory quota. This API is intended for trusted scripts. Path checks are not an operating-system sandbox and do not protect against another process replacing the directory tree during an operation. Do not authorize directories whose links may be changed concurrently.

## Modules and asynchronous execution

Entry paths may be absolute or relative within the root directory. Dependencies must use `./` or `../` paths with explicit extensions, and resolved paths must remain inside the root. UTF-8 files may include a BOM. Automatic extension completion, package names, URLs, and native modules are not supported.

Modules are cached by canonical path. The same path may have multiple aliases, but an existing alias cannot be overwritten. Module state lasts until the context is destroyed; changes to files are not hot-reloaded. Circular dependencies follow ECMAScript rules: bindings cannot be read before initialization.

When a function returns a Promise, the plugin processes the job queue and waits for it to settle. Modules support top-level `await`. If a return value or module initialization remains pending with no runnable jobs, the call returns `E_PENDING`. Unhandled Promise rejections are detected. A pending Promise that is not returned and has no further jobs does not determine whether the operation is complete.

There is no external event loop, timer API, or asynchronous I/O Promise API. Execution deadlines cover JavaScript execution, the job queue, and JSON serialization, but cannot interrupt Windows filesystem calls.

## Return values and errors

Results follow the built-in `JSON.stringify` semantics, regardless of whether a script replaces the global `JSON.stringify` function. Top-level `undefined` produces an empty string; a JavaScript empty string produces `""`. Non-finite numbers produce `null` according to JSON rules. Unsupported values such as circular references, BigInt, and top-level functions return an error.

NSIS limits input capacity. The plugin cannot recover arguments that NSIS truncated before the call. Output is checked against the caller's actual capacity; truncated JSON is never written. Error messages may be shortened and end with the literal marker `...[截短]` (meaning "truncated").

| Status | Meaning |
| --- | --- |
| `0` | Success |
| `E_ARGUMENT` | Invalid arguments, alias, or function |
| `E_STATE` | Uninitialized, already initialized, or invalid context; concurrent call or wrong thread |
| `E_JS` | Syntax, runtime, module import, or Promise rejection error |
| `E_PATH` | Invalid entry path or path outside the root |
| `E_IO` | Unreadable entry file or file size limit exceeded |
| `E_ENCODING` | Invalid UTF-8 or UTF-16 encoding |
| `E_SERIALIZE` | Result cannot be converted to JSON |
| `E_RESULT_TOO_LONG` | Output exceeds NSIS string capacity |
| `E_TIMEOUT` | JavaScript execution timed out |
| `E_MEMORY` | Engine allocation limit reached or allocation failed |
| `E_PENDING` | Result or module initialization Promise cannot settle |
| `E_INTERNAL` | Internal plugin error |

Module dependency loading errors are reported through JavaScript exceptions, so the outer status is `E_JS`. Ordinary exceptions preserve existing state and do not roll back side effects. If an exception leaves jobs in the queue, the context is invalidated to prevent those jobs from affecting the next call.

After `E_TIMEOUT`, `E_MEMORY`, `E_PENDING`, or an internal runtime error, call `Destroy` and then `Init`. `E_RESULT_TOO_LONG` indicates only a transmission failure; the script has already executed and its side effects remain. Ordinary `E_JS` errors include the message and any available stack trace. Thrown values that are not Error objects do not necessarily provide a stack trace.
