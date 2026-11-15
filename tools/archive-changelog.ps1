# 把 CHANGELOG.md 里除最新版本外的旧段落归档到 CHANGELOG.old 顶部。
#
# 约定：CHANGELOG.md 只保留最新的一个 "## vX.Y.Z" 段落（新版本写在最前），
#       发版后运行本脚本，旧版本按原有顺序移入 CHANGELOG.old，归档文件只增不改。
#
# 用法：
#   powershell -File tools/archive-changelog.ps1           # 归档
#   powershell -File tools/archive-changelog.ps1 -DryRun   # 只看会动哪些版本
[CmdletBinding()]
param(
    [string]$ChangeLog,
    [string]$Archive,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
# $PSScriptRoot / $PSCommandPath 在 param 默认值求值时可能还没赋值
# （powershell -File 的 5.1 实测如此），默认路径改到这里再拼
$here = Split-Path -Parent $PSCommandPath
if (-not $ChangeLog) { $ChangeLog = Join-Path $here '..\CHANGELOG.md' }
if (-not $Archive)   { $Archive   = Join-Path $here '..\CHANGELOG.old' }
$ChangeLog = (Resolve-Path $ChangeLog).Path
$Archive   = [IO.Path]::GetFullPath($Archive)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$text = [IO.File]::ReadAllText($ChangeLog)
$headings = [regex]::Matches($text, '(?m)^##\s+(v\d+(?:\.\d+)*)')
if ($headings.Count -le 1) {
    Write-Host "CHANGELOG.md 只有 $($headings.Count) 个版本段落，无需归档。"
    exit 0
}

# 头部说明 + 最新一段留在原处，其余整段搬走
$head = $text.Substring(0, $headings[0].Index)
$keep = $text.Substring($headings[0].Index, $headings[1].Index - $headings[0].Index).TrimEnd() + "`n"
$old  = $text.Substring($headings[1].Index).TrimEnd() + "`n"

$versions = @($headings | ForEach-Object { $_.Groups[1].Value })
$dropped  = $versions[1..($versions.Count - 1)]

if ($DryRun) {
    Write-Host "保留：$($versions[0])"
    Write-Host "归档：$($dropped -join ', ')"
    exit 0
}

if (Test-Path $Archive) {
    $archived = [IO.File]::ReadAllText($Archive)
    $old = $old + "`n" + $archived.TrimStart()
}
[IO.File]::WriteAllText($Archive, $old, $utf8NoBom)
[IO.File]::WriteAllText($ChangeLog, $head + $keep, $utf8NoBom)

Write-Host "保留 $($versions[0])，归档 $($dropped.Count) 个旧版本到 $Archive"
