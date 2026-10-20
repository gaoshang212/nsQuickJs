# nsQuickJs

**English** | [简体中文](README.zh-CN.md)

An open-source NSIS plugin for running JavaScript, powered by QuickJS-NG and released under the MIT license.

- Execute JavaScript strings and UTF-8 script files.
- Read and write UTF-8 files and manage directories through `fs` when authorized by NSIS; file access is disabled by default.
- Preserve the execution context to reuse variables, functions, and module state.
- Load ES modules and call exported functions with JSON arguments.
- Support Promises that can settle through the JavaScript job queue, including top-level `await` in modules.
- Enforce execution time and memory limits, with explicit error codes.
- Build plugins for both `x86-unicode` and `amd64-unicode` installers.

## Download

[Releases](https://github.com/gaoshang212/nsQuickJs/releases)

## Compatibility

| Component | Scope and validation |
| --- | --- |
| Installer ABI | Unicode NSIS, `x86-unicode` and `amd64-unicode`; ANSI and native ARM64 plugins are not provided |
| NSIS | Tested with toolchains built from pinned NSIS 3.13 sources; older versions are not validated |
| Windows | Locally tested on build 10.0.26300; a minimum supported Windows version has not been established |
| Runtime dependencies | Release DLLs statically link QuickJS-NG and the C runtime; users do not need to install them separately |

The local tests cover both architectures in Debug and Release. A separate clean Windows system without development tools has not yet been tested. See the [validation record](docs/validation.md) for results and remaining checks.

## Usage

Keep the release package's `Plugins`, `Include`, and `Examples` directories in their original relative locations. Standard NSIS Unicode installers use the x86 plugin; native x64 installers use the amd64 plugin. Choose the plugin architecture based on the installer process, not the operating system or the application being installed.

```nsis
Unicode true
Target x86-unicode
!define NSQUICKJS_PLUGIN_DIR "path\to\nsQuickJs\Plugins"
!include "path\to\nsQuickJs\Include\nsQuickJs.nsh"

; Place these calls inside a Section. Use forward slashes or escaped backslashes in JSON paths.
!insertmacro NsQuickJs_Init '{"rootDir":"C:/my-installer/scripts"}' $0 $1
; Check $0 after every call; only "0" indicates success.
!insertmacro NsQuickJs_Eval '1 + 2' $0 $1
; $1 contains the JSON text 3.
!insertmacro NsQuickJs_LoadModule 'main' 'main.mjs' $0 $1
!insertmacro NsQuickJs_Call 'main' 'calculate' '[20,22]' $0 $1
!insertmacro NsQuickJs_Destroy $0 $1
```

See [examples/demo.nsi](examples/demo.nsi) for script extraction and error handling. A module can export a function like this:

```js
// Return a Promise whose result is serialized to JSON for NSIS.
export async function calculate(a, b) {
    return { total: await Promise.resolve(a + b) };
}
```

For x64, set `Target amd64-unicode` and use an NSIS toolchain that includes amd64 installer stubs. This project provides a script to build test toolchains from pinned NSIS sources.

### Execute a script file: NsQuickJs_EvalFile

For longer scripts, save the code in a UTF-8 file and execute it with `NsQuickJs_EvalFile`. For example, save the following as `calculate.js`:

```js
// Define a global function that can also be called with an empty NsQuickJs_Call alias.
function add(a, b) {
    return a + b;
}
// The final expression supplies the script result; NSIS receives the JSON text 42.
add(20, 22);
```

The file must already exist inside `rootDir` at runtime. With the configuration above, its location is `C:/my-installer/scripts/calculate.js`. To bundle the script with an installer, extract it with NSIS `SetOutPath` and `File`, then initialize `rootDir` with that directory.

Place these calls inside a `Section`, after a successful `Init` and before `Destroy`:

```nsis
; The path is relative to rootDir, not the working directory. $0/$1 receive status and result.
!insertmacro NsQuickJs_EvalFile 'calculate.js' $0 $1
StrCmp $0 '0' eval_file_ok
  DetailPrint "Script execution failed: $0 / $1"
  SetErrorLevel 1
  Abort
eval_file_ok:
  DetailPrint "Script file result: $1" ; Prints 42 in this example.
```

Script files may include a UTF-8 BOM and must not exceed 16 MiB. Each call reads and executes the file again; variables and functions remain in the shared context. Use `EvalFile` for ordinary scripts and `NsQuickJs_LoadModule` for ES modules containing static `import`, `export`, or top-level `await`. See the [EvalFile reference](docs/api.md#execute-script-files-evalfile) for parameter, path, and return-value rules.

### File access and permissions

The global JavaScript object `fs` provides synchronous file operations, which are disabled by default. Enable them during initialization with a JSON boolean:

```nsis
; rootDir must exist. This configuration permits file access within that directory.
!insertmacro NsQuickJs_Init '{"rootDir":"C:/my-installer/scripts","allowFileAccess":true}' $0 $1
; Check that $0 is 0 before executing scripts.
```

After initialization, NSIS can also enable or disable access dynamically:

```nsis
; ENABLED accepts only true or false. Check $0 after every call.
!insertmacro NsQuickJs_SetFileAccess 'true' $0 $1
!insertmacro NsQuickJs_EvalFile 'files.js' $0 $1
; Revoking access also blocks calls through previously saved fs method references.
!insertmacro NsQuickJs_SetFileAccess 'false' $0 $1
```

For example, `files.js` can write and read JSON:

```js
// Paths are relative to rootDir. The script handles JSON serialization.
fs.writeText('settings.json', JSON.stringify({ language: 'zh-CN' }));
// The final expression supplies the EvalFile result.
JSON.parse(fs.readText('settings.json'));
```

Other methods include `appendText`, `exists`, `mkdir`, `readDir`, `remove`, and `stat`. Only the host can change permissions; JavaScript cannot enable access itself. Disabling file access does not disable `EvalFile` or module loading. Operations are restricted to `rootDir`, with a 16 MiB text-file limit. Binary file I/O and the Node.js `fs` API are not supported. See the [file access reference](docs/api.md#javascript-file-access-fs) for methods and errors. The [demo](examples/demo.nsi) shows authorization, script execution, and permission revocation.

## Build and test

Requirements: Visual Studio 2022 with the Desktop development with C++ workload, Windows SDK, CMake 3.24+, and Git. Building the installer test toolchain also requires Python 3.8+.

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

Run these commands from the repository root. The initial configuration downloads a pinned QuickJS-NG source archive and verifies its SHA-256 checksum. Install both architectures into the same directory to produce a combined package.

For details, see the [build guide](docs/build.md) and [API reference](docs/api.md).

## Limitations

This plugin is intended for trusted, short-running data-processing scripts. Synchronous calls block the installer thread. It does not provide Node.js, npm, networking, timers, registry access, or process APIs. File access requires explicit host authorization.

NSIS stack strings typically hold up to 1024 UTF-16 code units, including the terminating null character. Use `EvalFile` for larger scripts; oversized results return `E_RESULT_TOO_LONG`. Pass user input through `Call` JSON argument arrays rather than concatenating it into `Eval` source code.

Path restrictions are not an operating-system sandbox for untrusted code. Other processes must not change directory links during file operations. Scripts can overwrite or delete existing files inside the authorized directory. JavaScript execution deadlines cannot interrupt blocking Windows file I/O; keep scripts and data on local storage.
