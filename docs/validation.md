# Initial Validation Record

Validation date: 2026-10-16. Environment: Windows 10.0.26300, VS 2022 / MSVC 19.44, CMake 3.31.6, and NSIS 3.13.

| Build | Runtime checks | DLL ABI checks | Real installer |
| --- | --- | --- | --- |
| x86 Debug | 83 passed | 18 passed | Passed |
| x86 Release | 83 passed | 18 passed | Passed |
| x64 Debug | 83 passed | 18 passed | Passed |
| x64 Release | 83 passed | 18 passed | Passed |

- A fresh build directory downloaded and verified the QuickJS-NG source archive; the x64 Release build and tests passed.
- A fresh dependency cache built the x64 toolchain from pinned NSIS and zlib commits; real installer tests passed.
- Release package examples for both architectures compiled and ran successfully in silent mode.
- PE checks confirmed matching installer and DLL architectures. Both Release DLLs imported only `KERNEL32.dll`.
- The workflow YAML was parsed and validated, but had not been executed on remote GitHub Actions runners.

Testing did not include a separate Windows 10 virtual machine or a system without development tools installed. Static dependency checks do not replace runtime validation on those systems.

## File access and permission validation

Validation date: 2026-10-19, using the local toolchain listed above.

| Build | Existing runtime checks | New file access checks | DLL ABI checks | Real installer |
| --- | --- | --- | --- | --- |
| x86 Debug | 83 passed | 77 passed | 23 passed | Passed |
| x86 Release | 83 passed | 77 passed | 23 passed | Passed |
| x64 Debug | 83 passed | 77 passed | 23 passed | Passed |
| x64 Release | 83 passed | 77 passed | 23 passed | Passed |

- File tests used isolated temporary directories to cover disabled-by-default access, configuration-based authorization, dynamic permissions, revocation of saved function references, and reinitialization.
- Coverage included Chinese paths, UTF-8, BOMs, embedded null characters, overwrite/append/delete operations, directory and metadata queries, size limits, argument errors, and encoding errors.
- Tests verified path boundaries, root-directory deletion protection, device names, and alternate data streams. Symbolic link, dangling link, and hard-link write protection cases all ran and passed on the local machine.
- Real NSIS installers verified the new export and macro, file I/O with Chinese text, and permission revocation. PE architectures matched.
- Updated release package demos for both architectures compiled and ran successfully in silent mode, including the `EvalFile` file I/O example.

This validation did not rerun remote GitHub Actions or test isolation against malicious concurrent replacement of the directory tree. The file API is intended for trusted scripts.

## Release preparation validation

Validation date: 2026-10-20, using the local toolchain listed above.

- The readiness audit rebuilt x86/x64 Debug and Release; all four configurations passed the `runtime` and `plugin` CTest targets and real installer tests.
- After the packaging changes, both Release configurations were rebuilt and their native and real installer tests passed again.
- Both architectures were installed into a fresh staging directory. The packaged x86 and amd64 examples compiled and ran successfully in silent mode.
- The packaging script ran successfully in PowerShell 7 and Windows PowerShell 5.1. Tests confirmed rejection of a mismatched tag, stale project files, a missing DLL, a DLL of the wrong architecture, and an output directory inside the package.
- The generated ZIP included both DLLs, current macros without `/NOUNLOAD`, both READMEs, dependency licenses, the changelog, and the security policy. The SHA-256 file matched the archive.
- Workflow and issue-template YAML parsed successfully. Local documentation links and `git diff --check` passed.

The GitHub repository had not yet been created, so hosted Actions, Release publication,
and private vulnerability reporting settings remain unverified. A clean Windows
system without development tools also remains untested. The release checklist in
`docs/build.md` requires those checks before publishing.
