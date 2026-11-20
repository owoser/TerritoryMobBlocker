<#
.SYNOPSIS
  从本机已安装的 xmake 包目录打包 LeviLamina 预编译 SDK，供 CI 分发使用。

.DESCRIPTION
  LeviLamina 本体从源码构建要编译上千个 TU（CI 上十几分钟），所以本仓库仍分发
  「预编译 SDK」给 CI 用：本脚本从本机已安装的 xmake 包里取出 include/ + lib/ 打包，
  再把新的 sha256 填回包定义即可升级 SDK 版本。

  为什么不再走源码构建？见 xmake-repo-extra/packages/l/levilamina/xmake.lua 顶部说明。

.EXAMPLE
  # 打包本机已安装的 26.60.2
  pwsh tools/build-sdk.ps1 -Version 26.60.2

  # 输出到指定目录
  pwsh tools/build-sdk.ps1 -Version 26.60.2 -OutputDir D:\tmp
#>
[CmdletBinding()]
param(
    [string] $Version = "26.60.2",
    [string] $TargetType = "server",
    [string] $OutputDir = "$PSScriptRoot\..\bin"
)

$ErrorActionPreference = "Stop"

function Get-PackageDir([string] $version, [string] $targetType) {
    $root = Join-Path $env:LOCALAPPDATA ".xmake\packages\l\levilamina\$version"
    if (-not (Test-Path $root)) {
        throw "找不到 xmake 包目录：$root`n先在本机跑一次 'xmake f -p windows -a x64 -m release --toolchain=clang-cl -y' 让 SDK 安装完成。"
    }
    # 版本目录下是若干由配置推导出的哈希目录：target_type 不同会落到不同目录，
    # 因此不能直接取第一个，必须按 manifest.txt 里的 target_type 精确挑选，
    # 否则可能把 client SDK 当成 server 打包。
    $candidates = Get-ChildItem $root -Directory | Where-Object {
        (Test-Path (Join-Path $_.FullName "include")) -and (Test-Path (Join-Path $_.FullName "lib"))
    }
    if (-not $candidates) {
        throw "$root 下没有已安装完成的包（缺 include/ 或 lib/），可能是上一次安装失败。"
    }
    $pattern = 'target_type\s*=\s*"' + [regex]::Escape($targetType) + '"'
    $matched = $candidates | Where-Object {
        $manifest = Join-Path $_.FullName "manifest.txt"
        (Test-Path $manifest) -and ((Get-Content -Raw $manifest) -match $pattern)
    }
    if (-not $matched) {
        throw "$root 下没有 target_type = $targetType 的包；可用目录：$($candidates.Name -join ', ')"
    }
    return ($matched | Select-Object -First 1).FullName
}

$pkgDir = Get-PackageDir $Version $TargetType
Write-Host "源包目录: $pkgDir"

$stage = Join-Path $env:TEMP "levilamina-sdk-$Version-$TargetType"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

# 只需要 include 与 lib；bin/ 下的运行期 DLL 不进构建依赖包
Copy-Item -Recurse (Join-Path $pkgDir "include") $stage
Copy-Item -Recurse (Join-Path $pkgDir "lib")     $stage

$readme = @"
LeviLamina SDK (prebuilt for xmake) - $Version / $TargetType

由 tools/build-sdk.ps1 打包。只含 include/ 与 lib/，不含 bin/ 下的运行期 DLL。
传递依赖（entt / fmt / leveldb / nlohmann_json / magic_enum / pfr 等）由 xmake 正常安装。
"@
Set-Content -Path (Join-Path $stage "README.txt") -Value $readme -Encoding utf8NoBOM

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$zip = Join-Path $OutputDir "levilamina-sdk-$Version-$TargetType.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$sizeMB = [math]::Round((Get-Item $zip).Length / 1MB, 1)

Write-Host ""
Write-Host "已生成: $zip  ($sizeMB MB)"
Write-Host "SHA256: $hash"
Write-Host ""
Write-Host "下一步："
Write-Host "  1. 作为 prerelease 资产上传（不要覆盖正式 Release，避免抢占 Latest）："
Write-Host "       gh release create sdk-v$Version `"$zip`" --title `"LeviLamina $Version SDK (prebuilt)`" --prerelease --target main"
Write-Host "  2. 更新 xmake-repo-extra/packages/l/levilamina/xmake.lua："
Write-Host "       add_versions(`"sdk:$Version`", `"$hash`")"
Write-Host "  3. 同步 xmake.lua 里的版本号（add_requires / add_requireconfs）后提交。"
