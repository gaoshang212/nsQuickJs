param(
    # 已安装的双架构包目录；脚本只读取其中的文件。
    [string]$PackageDirectory = 'dist/nsQuickJs',
    # 归档、SHA-256 和发布说明的输出目录，不允许位于包目录内。
    [string]$OutputDirectory = 'dist',
    # 可选稳定版标签；省略时从 CMake 项目版本生成，指定时必须完全匹配。
    [string]$Tag
)
# 校验版本、包内容与架构后生成发行文件；任一步失败即终止。
$ErrorActionPreference = 'Stop'
# 仓库根目录，用于读取版本和核对待发布的项目文件。
$repo = Split-Path $PSScriptRoot -Parent
# CMake 是版本的唯一来源，避免标签和文件名各自维护版本。
$versionMatch = [regex]::Match([IO.File]::ReadAllText((Join-Path $repo 'CMakeLists.txt')),
    '(?m)^project\(nsQuickJs VERSION (\d+\.\d+\.\d+) LANGUAGES C CXX\)')
if (!$versionMatch.Success) { throw '无法读取 CMake 项目版本' }
# 不带 v 前缀的三段版本号，用于查找更新日志。
$version = $versionMatch.Groups[1].Value
if (!$Tag) { $Tag = "v$version" }
if ($Tag -cne "v$version") { throw "标签 $Tag 与项目版本 v$version 不一致；仅支持稳定版标签" }
# 当前版本的更新日志正文；不将其他版本内容混入发布说明。
$notesPattern = '(?ms)^## \[' + [regex]::Escape($version) + '\][^\r\n]*\r?\n(?<body>.*?)(?=^## |\z)'
# 必须有非空版本说明，避免发布空白或错误版本的 Release。
$notesMatch = [regex]::Match([IO.File]::ReadAllText((Join-Path $repo 'CHANGELOG.md')), $notesPattern)
if (!$notesMatch.Success -or !$notesMatch.Groups['body'].Value.Trim()) { throw "CHANGELOG.md 缺少 $version 的发布说明" }
# 将相对目录解析为绝对路径，确保归档不会包含自己。
$package = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackageDirectory)
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (!(Test-Path -LiteralPath $package -PathType Container)) { throw "包目录不存在：$package" }
if ($output.TrimEnd('\', '/') -ieq $package.TrimEnd('\', '/') -or
    $output.StartsWith($package.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '输出目录不能位于包目录内'
}

# 比较包内项目文件与当前工作树；缺失或内容过期时拒绝打包。
# Source 为仓库内相对路径，Destination 为包内相对路径；无返回值。
function Assert-CurrentFile([string]$Source, [string]$Destination) {
    # 源文件和待发布文件的绝对路径；两者均只读。
    $original = Join-Path $repo $Source
    $installed = Join-Path $package $Destination
    if (!(Test-Path -LiteralPath $installed -PathType Leaf)) { throw "发布包缺少：$Destination" }
    if ((Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash) {
        throw "发布包文件已过期：$Destination；请重新执行 cmake --install"
    }
}

# 读取 Path 的 PE Machine 字段并核对 Expected；格式或架构错误时抛出异常。
function Assert-Machine([string]$Path, [uint16]$Expected) {
    # 二进制读取器拥有文件流，finally 保证关闭。
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        $reader.BaseStream.Position = 0x3c
        # DOS 头的 e_lfanew 保存 PE 签名的文件偏移，单位为字节。
        $offset = $reader.ReadUInt32()
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne $Expected) {
            throw "DLL 的 PE 架构不符：$Path"
        }
    } finally { $reader.Dispose() }
}

# 根目录公共文件与 CMake 的安装清单保持一致。
foreach ($name in @('README.md', 'README.zh-CN.md', 'CHANGELOG.md', 'SECURITY.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')) {
    Assert-CurrentFile $name $name
}
# 项目源码目录到发布包目录的映射，保留包中既有大小写。
$directories = @{ 'docs' = 'docs'; 'include' = 'Include'; 'examples' = 'Examples' }
# 遍历映射和各目录内的文件，覆盖全部文档、宏与示例源码。
foreach ($directory in $directories.Keys) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $repo $directory) -Recurse -File) {
        # 相对后缀不含目录分隔符前缀，用于拼接两侧路径。
        $relative = $file.FullName.Substring((Join-Path $repo $directory).Length + 1)
        Assert-CurrentFile (Join-Path $directory $relative) (Join-Path $directories[$directory] $relative)
    }
}
Assert-CurrentFile 'third_party/nsis/COPYING' 'licenses/NSIS.txt'
# QuickJS-NG 许可证由构建树安装，发布任务中不重新下载引擎。
$engineLicense = Join-Path $package 'licenses/QuickJS-NG.txt'
if (!(Test-Path -LiteralPath $engineLicense -PathType Leaf) -or (Get-Item -LiteralPath $engineLicense).Length -eq 0) {
    throw '发布包缺少 QuickJS-NG 许可证'
}
Assert-Machine (Join-Path $package 'Plugins/x86-unicode/nsQuickJs.dll') 0x14c
Assert-Machine (Join-Path $package 'Plugins/amd64-unicode/nsQuickJs.dll') 0x8664

New-Item -ItemType Directory -Force -Path $output | Out-Null
# 统一采用带 v 前缀的文件名，与 GitHub 标签和下载说明一致。
$archiveName = "nsQuickJs-$Tag.zip"
$archive = Join-Path $output $archiveName
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $archive -Force
# 校验文件采用常见的“哈希、两个空格、文件名”格式，使用 ASCII 编码。
$digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archive.sha256", "$digest  $archiveName`n", [Text.Encoding]::ASCII)
# 发布正文来自当前版本的更新日志，使用不带 BOM 的 UTF-8。
$notes = "# nsQuickJs $Tag`n`n" + $notesMatch.Groups['body'].Value.Trim() + "`n"
[IO.File]::WriteAllText((Join-Path $output "nsQuickJs-$Tag.notes.md"), $notes, [Text.UTF8Encoding]::new($false))
Write-Host "已生成 $archive 及 SHA-256 校验文件、发布说明"
