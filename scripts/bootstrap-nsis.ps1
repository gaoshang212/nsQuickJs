param(
    # 目标架构；all 顺序构建两套工具链，共用缓存但不并行写入。
    [ValidateSet('x86', 'x64', 'all')][string]$Arch = 'all',
    # CMake 可执行文件名或绝对路径，用于编译 zlib。
    [string]$CMake = 'cmake',
    # Python 可执行文件，用于安装并运行固定版本的 SCons。
    [string]$Python = 'python',
    # 可选缓存目录；省略时使用仓库内 .cache，不修改系统 NSIS。
    [string]$CacheDirectory
)
# 在当前缓存中构建最小 NSIS 编译器和 zlib 模板；任一步失败即终止。
$ErrorActionPreference = 'Stop'
# 从脚本位置定位仓库，允许从其他工作目录调用。
$repo = Split-Path $PSScriptRoot -Parent
# 依赖源码、构建产物和工具链的统一缓存根目录。
$cache = if ($CacheDirectory) { [IO.Path]::GetFullPath($CacheDirectory) } else { Join-Path $repo '.cache' }
New-Item -ItemType Directory -Force $cache | Out-Null
# 执行 Program 并按数组传递 Arguments；输出写入主机，非零退出码转为异常。
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "命令失败：$Program，退出码 $LASTEXITCODE" }
}
# 获取 Name 对应的依赖目录；Tag 用于浅克隆，最终检出 Commit 固定版本。
# 返回源码路径；切换已有缓存版本前拒绝覆盖本地修改。
function Get-Source([string]$Name, [string]$Url, [string]$Commit, [string]$Tag) {
    $path = Join-Path $cache $Name
    $fresh = !(Test-Path (Join-Path $path '.git'))
    if ($fresh) {
        Invoke-Checked git @('clone', '--depth', '1', '--branch', $Tag, '--no-checkout', $Url, $path)
    }
    $current = & git -C $path rev-parse HEAD
    if ($fresh -or $LASTEXITCODE -ne 0 -or $current -ne $Commit) {
        if (!$fresh) {
            $changes = & git -C $path status --porcelain
            if ($changes) { throw "依赖目录有本地修改：$path" }
        }
        Invoke-Checked git @('-C', $path, 'fetch', '--depth', '1', 'origin', $Commit)
        Invoke-Checked git @('-C', $path, 'checkout', '--detach', $Commit)
    }
    return $path
}
# NSIS 3.13 源码目录，固定提交保证编译器与安装器模板来自同一版本。
$nsis = Get-Source 'nsis' 'https://github.com/kichik/nsis.git' 'a4ba28e60bd9e7c271cd2bfc1558889ce3355653' 'v313'
# zlib 1.3.1 源码目录，用于构建两种架构的静态压缩库。
$zlib = Get-Source 'zlib' 'https://github.com/madler/zlib.git' '51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf' 'v1.3.1'
# 私有 Python 包目录，避免将测试构建依赖安装到全局环境。
$pythonDeps = Join-Path $cache 'python'
if (!(Test-Path (Join-Path $pythonDeps 'SCons/__main__.py'))) {
    Invoke-Checked $Python @('-m', 'pip', 'install', '--target', $pythonDeps, 'scons==4.8.1')
}
# 保存调用者的模块路径，成功或失败均在 finally 中恢复。
$savedPythonPath = $env:PYTHONPATH
try {
    $env:PYTHONPATH = "$pythonDeps;$savedPythonPath"
    # 待构建的架构序列，共用源码目录须逐个构建。
    $architectures = if ($Arch -eq 'all') { @('x86','x64') } else { @($Arch) }
    foreach ($item in $architectures) {
        # VS 平台名与 NSIS 的目标名不同，分别用于 CMake 和 SCons。
        $platform = if ($item -eq 'x86') { 'Win32' } else { 'x64' }
        $target = if ($item -eq 'x86') { 'x86' } else { 'amd64' }
        # 按架构隔离 zlib 构建树、SDK 导出目录和最终 NSIS 工具链。
        $zbuild = Join-Path $cache "zlib-build-$item"
        $zsdk = Join-Path $cache "zlib-sdk-$item"
        $prefix = Join-Path $cache "nsis-toolchain-$item"
        Invoke-Checked $CMake @('-S', $zlib, '-B', $zbuild, '-G', 'Visual Studio 17 2022', '-A', $platform,
            '-DZLIB_BUILD_EXAMPLES=OFF', '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
        Invoke-Checked $CMake @('--build', $zbuild, '--config', 'Release', '--target', 'zlibstatic', '--parallel')
        New-Item -ItemType Directory -Force $zsdk | Out-Null
        Copy-Item -LiteralPath (Join-Path $zlib 'zlib.h'),(Join-Path $zbuild 'zconf.h') -Destination $zsdk
        # NSIS 构建脚本查找 zdll.lib；这里提供的是匹配 /MT 的静态 zlib 库。
        Copy-Item -LiteralPath (Join-Path $zbuild 'Release/zlibstatic.lib') -Destination (Join-Path $zsdk 'zdll.lib')
        Invoke-Checked $Python @('-X', 'utf8', '-m', 'SCons', '-C', $nsis, '-j4', '--config=force', "TARGET_ARCH=$target", 'UNICODE=yes',
            "APPEND_LINKFLAGS=gdi32.lib user32.lib shlwapi.lib oleaut32.lib version.lib shell32.lib $zsdk/zdll.lib",
            'VERSION=3.13', 'VER_PACKED=0x030D0000', "PREFIX=$prefix", "ZLIB_W32=$zsdk",
            'SKIPPLUGINS=all', 'SKIPUTILS=all', 'SKIPMISC=all', 'SKIPDOC=all', 'SKIPSTUBS=bzip2,lzma', 'install-compiler', 'stubs')
        # 编译器运行时需要的普通/固实 zlib 模板及卸载图标目录。
        $stubDir = Join-Path $prefix 'Stubs'
        New-Item -ItemType Directory -Force $stubDir | Out-Null
        foreach ($suffix in @("zlib-$target-unicode", "zlib_solid-$target-unicode")) {
            Copy-Item -LiteralPath (Join-Path $nsis "build/urelease/stub_$suffix/stub_zlib.exe") -Destination (Join-Path $stubDir $suffix)
        }
        Copy-Item -LiteralPath (Join-Path $nsis 'Source/exehead/uninst.ico') -Destination (Join-Path $stubDir 'uninst')
        Copy-Item -LiteralPath (Join-Path $nsis 'Include') -Destination $prefix -Recurse -Force
        if (!(Test-Path (Join-Path $prefix 'Bin/makensis.exe'))) { throw "NSIS 编译器未生成：$prefix" }
        Write-Host "NSIS $item 工具链：$prefix"
    }
} finally { $env:PYTHONPATH = $savedPythonPath }
