# Building and Releasing

## Toolchain

Install Visual Studio 2022 with the Desktop development with C++ workload, Windows SDK, CMake 3.24+, and Git.
Run the commands in the [English README](../README.md#build-and-test) from a Developer PowerShell. A [Chinese README](../README.zh-CN.md#构建和测试) is also available. If CMake is not on `PATH`, use the executables in `Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin` under the Visual Studio installation directory.

The presets use VS 2022: `x86` selects `Win32`, and `x64` selects `x64`. The `.def` file exports undecorated function names. The plugin links the C runtime statically with `/MT`, or `/MTd` for Debug builds. End users do not need to install QuickJS or the Visual C++ runtime.

After configuring the x64 preset, build and test Debug with:

```powershell
cmake --build build/x64 --config Debug --target nsQuickJs runtime_tests plugin_tests
ctest --test-dir build/x64 -C Debug --output-on-failure
```

If you already have the QuickJS-NG sources, configure offline with `-DFETCHCONTENT_SOURCE_DIR_QUICKJS=<absolute-path>`. You are responsible for ensuring that the source matches the pinned commit.

## NSIS toolchains for both architectures

`scripts/bootstrap-nsis.ps1` fetches pinned versions of NSIS, zlib, and SCons into `.cache`. It builds compilers and zlib installer stubs for both architectures from the same NSIS sources. It does not modify the system NSIS installation or require administrator privileges.

```powershell
./scripts/bootstrap-nsis.ps1 -Arch all
./scripts/test-installer.ps1 -Arch x86 -Configuration Release
./scripts/test-installer.ps1 -Arch x64 -Configuration Release
```

The bootstrap script accepts `-CMake` and `-Python` to select executables. Dependency caches can be reused, but do not run toolchain builds concurrently in the same workspace. To use an external complete NSIS toolchain, pass its compiler through the test script's `-MakeNSIS` argument.

The generated toolchain is the minimal version needed for testing, with zlib stubs and headers. It is not a complete NSIS distribution.

## Test layers

1. Runtime tests cover JavaScript, modules, JSON, exceptions, timeouts, memory, and lifetimes. `tests/file_tests.cpp` uses isolated temporary directories to test file operations, permission revocation, path boundaries, and link restrictions. Symbolic link tests report a skip when the system does not permit link creation.
2. DLL ABI tests load the actual plugin and verify exports, argument order, stack balance, string boundaries, and the unload callback.
3. Installer tests compile and run real NSIS installers, checking PE architecture, macro calls, shared state across calls, and error recovery.

The test installer extracts scripts only into its own temporary directory. It does not modify the registry or system directories. On failure, it writes a `.failure.txt` file beside the installer and returns a nonzero exit code. The external process timeout is 30 seconds.

## Packaging

Build both Release configurations from the intended source revision first. Use a fresh package directory so files removed from the source do not survive from a previous install. Install both architectures into the same prefix, then run the package validator:

```powershell
# Use a new directory for each packaging attempt.
$package = Join-Path 'dist' ('package-' + [guid]::NewGuid().ToString('N'))
cmake --install build/x86 --config Release --prefix $package
cmake --install build/x64 --config Release --prefix $package
./scripts/package-release.ps1 -PackageDirectory $package
```

The script reads the version from `CMakeLists.txt` and emits `dist/nsQuickJs-v0.1.0.zip`, `dist/nsQuickJs-v0.1.0.zip.sha256`, and a `.notes.md` file for the release body. It requires a matching nonempty version section in `CHANGELOG.md`, matching project files in the package, both DLL architectures, and dependency licenses. An explicit `-Tag` must exactly match `v<project-version>`; prerelease tags are not currently supported. `-OutputDirectory` can select another output directory outside the package tree.

The package includes both READMEs, the changelog, and the security policy. Compile `demo.nsi` from the package's `Examples` directory; use `/DDEMO_TARGET=amd64-unicode` to build the x64 example. Test both examples in silent mode before distributing a local package.

File checks detect stale documentation, macros, and example sources. PE checks verify DLL architecture; they do not prove which revision built the DLL. Always rebuild before packaging. GitHub Actions builds the DLLs and packages them in the same workflow run.

## Publishing a release

1. Confirm that `origin` points to [gaoshang212/nsQuickJs](https://github.com/gaoshang212/nsQuickJs). Enable private vulnerability reporting under the repository's security settings; `SECURITY.md` documents a fallback if it is unavailable.
2. Set the intended version in `CMakeLists.txt` and add its `## [X.Y.Z]` section to `CHANGELOG.md`. Keep both READMEs synchronized.
3. Commit all intended changes, including new files. Confirm `git status --short` is clean. Push `main` and wait for all four GitHub Actions build jobs to pass. Local tests do not validate the hosted runner environment.
4. Download the CI artifacts and verify the examples on a clean Windows system without development tools. Record the exact Windows build, NSIS version, architectures, and results in `docs/validation.md`. Push any resulting changes and wait for CI again before tagging.
5. Create the version tag on the validated commit and push it:

   ```powershell
   git tag -a v0.1.0 -m "Release v0.1.0"
   git push origin v0.1.0
   ```

6. The tag workflow reruns the full matrix, combines the Release artifacts, validates their version and content, and publishes the ZIP and checksum with the corresponding changelog section as release notes. It creates a public-facing release, not a draft. Inspect the uploaded assets and verify the downloaded checksum.

Ordinary commits and pull requests upload build artifacts without publishing a release. A `v*` tag triggers the release path, but packaging rejects tags that do not match the project version. Use a new version for changes to an already published release rather than moving the tag.
