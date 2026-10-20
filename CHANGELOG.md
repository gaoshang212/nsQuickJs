# Changelog

Version sections describe the release content. A section does not indicate that
the release has been published; published tags and assets appear in GitHub Releases.

## [0.1.0]

Initial release of the Unicode NSIS plugin, embedding QuickJS-NG 0.17.0.

### Features

- Execute JavaScript strings and UTF-8 files in a reusable context.
- Load ES modules and call JavaScript functions with JSON arguments and results.
- Settle Promises through the JavaScript job queue, including top-level `await`.
- Apply JavaScript execution time, memory, and stack limits.
- Access UTF-8 files and directories through a synchronous `fs` API, disabled by
  default and controlled by the NSIS host. Revocation also blocks saved method references.
- Provide x86 and amd64 Unicode DLLs, NSIS macros, examples, and English and Chinese READMEs.

### Fixes included

- Correct pending asynchronous module cleanup in the embedded engine.
- Keep the plugin loaded through `RegisterPluginCallback`, without redundant
  `/NOUNLOAD` flags in the NSIS macros.

### Compatibility and limitations

- Tested locally with NSIS 3.13 and Windows build 10.0.26300, in x86/x64 Debug and Release.
- Intended for trusted, short-running scripts. Path restrictions are not an OS sandbox.
- No Node.js, npm, timers, networking, registry access, or process APIs.
- File I/O is synchronous and cannot be interrupted by the JavaScript execution deadline.
- ANSI and native ARM64 plugins are not provided. Older NSIS versions and a
  minimum Windows version have not been validated.

See `docs/validation.md` in the source checkout or plugin package for the tests
performed and remaining checks.
