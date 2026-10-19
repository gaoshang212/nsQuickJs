param(
    # 安装器与插件共同使用的目标架构。
    [Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Arch,
    # 已构建插件的配置，须与 build/<Arch> 中的产物一致。
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    # 可选外部 NSIS 编译器；省略时使用 bootstrap-nsis.ps1 生成的工具链。
    [string]$MakeNSIS
)
# 构建并静默运行真实安装器；编译、架构或执行结果不符均使脚本失败。
$ErrorActionPreference = 'Stop'
# 仓库根目录，用于定位夹具、DLL 和测试安装器源码。
$repo = Split-Path $PSScriptRoot -Parent
if (!$MakeNSIS) { $MakeNSIS = Join-Path $repo ".cache/nsis-toolchain-$Arch/Bin/makensis.exe" }
# NSIS Unicode 插件目录名，必须与安装器目标架构相匹配。
$target = if ($Arch -eq 'x64') { 'amd64-unicode' } else { 'x86-unicode' }
# 对应架构的 CMake 构建树。
$build = Join-Path $repo "build/$Arch"
# 为本配置暂存插件，避免 Debug 和 Release 集成测试互相覆盖。
$pluginRoot = Join-Path $build "integration-$Configuration/Plugins"
# 待验证的实际构建 DLL，复制后交给 makensis 打包。
$dll = Join-Path $build "plugins/$target/$Configuration/nsQuickJs.dll"
# 本次测试安装器及其 .failure.txt 诊断文件的路径前缀。
$output = Join-Path $build "integration-$Configuration/installer.exe"
New-Item -ItemType Directory -Force (Join-Path $pluginRoot $target) | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $pluginRoot "$target/nsQuickJs.dll")
# 读取 Path 的 PE/COFF Machine 字段，返回架构编号；文件格式错误则抛出异常。
function Get-Machine([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        # DOS 头中的 e_lfanew 指向 PE 签名，签名后紧跟 Machine 字段。
        $stream.Position = 0x3c
        $offset = $reader.ReadUInt32()
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "不是 PE 文件：$Path" }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose() }
}
& $MakeNSIS /V2 "/DTEST_TARGET=$target" "/DTEST_OUTPUT=$output" "/DTEST_SOURCE=$repo" "/DTEST_PLUGINS=$pluginRoot" (Join-Path $repo 'tests/installer.nsi')
if ($LASTEXITCODE -ne 0) { throw 'NSIS 测试安装器编译失败' }
# 预期 COFF 编号：AMD64 为 0x8664，I386 为 0x14c。
$machine = if ($Arch -eq 'x64') { 0x8664 } else { 0x14c }
if ((Get-Machine $dll) -ne $machine -or (Get-Machine $output) -ne $machine) { throw '安装器或 DLL 的 PE 架构不符' }
# 静默安装器进程；最多等待 30 秒，防止回归引起 CI 永久挂起。
$process = Start-Process -FilePath $output -ArgumentList '/S' -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(30000)) { $process.Kill(); throw '测试安装器执行超时' }
if ($process.ExitCode -ne 0) {
    if (Test-Path "$output.failure.txt") { Get-Content -LiteralPath "$output.failure.txt" -Encoding Unicode | Write-Host }
    throw "测试安装器失败：$($process.ExitCode)"
}
Write-Host "${Arch} ${Configuration}：真实安装器测试通过，PE 架构正确"
