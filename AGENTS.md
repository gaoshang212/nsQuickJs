# Repository Guidelines

## Project Structure & Module Organization

nsQuickJs embeds QuickJS-NG in a Unicode NSIS plugin for x86 and x64. Keep responsibilities separated: `src/plugin.cpp` handles the NSIS ABI and string transport, `src/runtime.cpp` manages JavaScript execution and lifetimes, and `src/paths.cpp` handles paths and file reads. Public NSIS macros live in `include/nsQuickJs.nsh`.

`src/files.cpp` implements the synchronous JS `fs` API. Keep file access disabled by default and enforce host-controlled permission on every call, including saved function references. Preserve the `rootDir` boundary and test mutations in isolated temporary directories, never in repository fixtures or user directories.

`tests/` contains native harnesses, installer tests, and script fixtures; `examples/` contains a demo installer. Build helpers live in `cmake/` and `scripts/`; API and build documentation live in `docs/`. Treat `build/`, `.cache/`, and `dist/` as generated output.

## Build, Test, and Development Commands

Use Windows with Visual Studio 2022 C++ desktop tools, Windows SDK, CMake 3.24+, and Git. Run from a developer PowerShell:

```powershell
cmake --preset x86
cmake --build --preset x86-release
ctest --preset x86-release
```

These configure, compile, and test the x86 Release build. Repeat with `x64` and `x64-release` for x64.

```powershell
./scripts/bootstrap-nsis.ps1 -Arch all
./scripts/test-installer.ps1 -Arch x86 -Configuration Release
./scripts/test-installer.ps1 -Arch x64 -Configuration Release
```

These prepare both NSIS toolchains and run real installers. Do not run toolchain bootstrap processes concurrently in one workspace. See `docs/build.md` for Debug builds and packaging.

## Coding Style & Naming Conventions

Follow `.editorconfig`: four spaces by default; two for JSON, YAML, CMake, and NSIS files. Use UTF-8 and LF, except PowerShell files use UTF-8 BOM and CRLF. C++ uses C++17, `nsq`, PascalCase types, camelCase methods, and trailing underscores for members. Preserve `NsQuickJs_*` macro naming. Keep explanations in Chinese and comments short, following `CONTRIBUTING.md`. No formatter or linter is configured.

## Comment Requirements

All new or modified project-owned code must include concise Chinese comments. Document every class/struct, function/method (including private methods, constructors, destructors, operators, callbacks, and nontrivial lambdas), member, and global/static variable. Explain purpose, parameter/return meaning, ownership, units, and error behavior where relevant. Use `///` for C++ declarations and `//` for implementation details; use the appropriate comment syntax in scripts and NSIS macros. Keep `.def` comments ASCII for MSVC linker compatibility. Declaration comments may serve definitions, but explain non-obvious implementation constraints locally. Update comments whenever behavior changes; avoid merely restating statements. Apply these rules to tests, examples, and build helpers too. Preserve upstream third-party sources, generated files, and byte-sensitive fixtures; document fixture intent in adjacent documentation when necessary.

## Testing Guidelines

CTest runs custom C++ harnesses named `runtime` and `plugin`. Add regression cases to `tests/runtime_tests.cpp` or `tests/plugin_tests.cpp`, with scripts in `tests/fixtures/`. Preserve the binary `invalid-utf8.js` fixture. Runtime changes require corresponding tests; module or ABI changes also require installer tests. Validate x86/x64 in Debug/Release for dependency updates. No numerical coverage threshold is configured.

## Commit & Pull Request Guidelines

Write commit subjects and bodies in English. Use concise Conventional Commit subjects, such as `feat: add file access permissions`. PRs should describe behavior changes, link relevant issues, and report tested architectures/configurations. Include minimal `.nsi`/JS reproductions for fixes. Keep exports, error codes, and macros consistent across architectures; update `docs/api.md` for public API changes. Pin dependency revisions and review licenses when updating them.
