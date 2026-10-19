# 构建与发布

## 工具链

安装 Visual Studio 2022 的 C++ 桌面开发组件、Windows SDK、CMake 3.24+、Git。
在开发者 PowerShell 中执行 README 命令。若 CMake 未加入 PATH，可使用 Visual Studio 安装目录下 `Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin` 的程序。

预设固定使用 VS 2022，`x86` 对应 `Win32`，`x64` 对应 `x64`。通过 `.def` 导出未修饰函数名；静态链接 `/MT`，Debug 使用 `/MTd`。最终用户不需要安装 QuickJS 或 VC 运行库。

```powershell
cmake --build build/x64 --config Debug --target nsQuickJs runtime_tests plugin_tests
ctest --test-dir build/x64 -C Debug --output-on-failure
```

已有 QuickJS-NG 源码时可通过 `-DFETCHCONTENT_SOURCE_DIR_QUICKJS=绝对路径` 离线配置，须自行保证对应锁定提交。

## NSIS 双架构工具链

`scripts/bootstrap-nsis.ps1` 在 `.cache` 内获取固定版本的 NSIS、zlib、SCons，从相同 NSIS 源码构建两种架构的编译器和 zlib 安装器模板。不修改系统 NSIS，不要求管理员权限。

```powershell
./scripts/bootstrap-nsis.ps1 -Arch all
./scripts/test-installer.ps1 -Arch x86 -Configuration Release
./scripts/test-installer.ps1 -Arch x64 -Configuration Release
```

脚本可传入 `-CMake` 和 `-Python` 指定可执行文件。依赖缓存允许重用，不应并行运行同一工作区内的工具链构建脚本。外部完整 NSIS 工具链可通过测试脚本的 `-MakeNSIS` 参数指定。

这里构建的是测试所需的最小 NSIS 工具链，只包含 zlib 模板和头文件，不是完整 NSIS 发行包。

## 测试层次

1. 运行时测试：JS、模块、JSON、异常、超时、内存及生命周期；`tests/file_tests.cpp` 在独立临时目录验证文件读写、授权撤销、路径边界和链接限制。符号链接测试在系统不允许创建链接时报告跳过。
2. DLL ABI 测试：动态加载真实插件，验证导出、参数顺序、栈平衡、字符串边界及退出回调。
3. 安装器测试：编译、运行真实 NSIS 安装器，检查 PE 架构、宏调用、跨调用状态和错误恢复。

测试安装器只在自己的临时目录释放脚本，不修改注册表或系统目录。失败时在安装器旁写入 `.failure.txt`，返回非零退出码。外部进程超时为 30 秒。

## 打包

将两个 Release 构建安装到相同前缀：

```powershell
cmake --install build/x86 --config Release --prefix dist/nsQuickJs
cmake --install build/x64 --config Release --prefix dist/nsQuickJs
Compress-Archive -Path dist/nsQuickJs/* -DestinationPath dist/nsQuickJs-0.1.0.zip -Force
```

在发布包的 `Examples` 目录编译 `demo.nsi`，使用 `/DDEMO_TARGET=amd64-unicode` 可生成 x64 示例。

GitHub Actions 在 Windows runner 上运行两种架构、两种配置的测试。`v*` 标签仅在完整测试矩阵成功后打包 Release 产物并创建 GitHub Release。普通提交和 PR 只上传构建产物。
