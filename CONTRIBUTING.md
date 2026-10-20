# Contributing

- Keep exported interfaces, error codes, and macro behavior consistent across x86 and x64.
- Add corresponding tests when changing runtime behavior. Run real installer tests for module or ABI changes.
- Write concise implementation comments in Chinese, including comments for private methods and members. Update `docs/api.md` when changing public interfaces.
- Keep the default English `README.md` and Chinese `README.zh-CN.md` synchronized, including their language links. Write `CONTRIBUTING.md` and documents under `docs/` in English. Documentation example comments should match the document language, and links must follow renamed headings.
- Write Git commit subjects and bodies in English. Follow Conventional Commits, for example: `feat: add file access permissions`.
- When updating dependencies, pin revisions, review licenses, and rerun Debug and Release tests for both architectures.
- When reporting an issue, include the Windows, NSIS, and plugin versions, together with a minimal `.nsi`/JavaScript reproduction.
- Use the repository's issue templates for bugs and feature requests. Follow [SECURITY.md](SECURITY.md) for private vulnerability reports.
- Describe user-visible changes in [CHANGELOG.md](CHANGELOG.md). Follow the [release checklist](docs/build.md#publishing-a-release) before publishing a version.

Keep implementation responsibilities separated:

- `src/plugin.cpp`: NSIS ABI and string transport.
- `src/runtime.cpp`: runtime state, invocation, resource lifetimes, and host-controlled file permissions.
- `src/paths.cpp`: script path resolution, text encoding, and script file reads.
- `src/files.cpp`: synchronous JavaScript file operations within `rootDir`.
