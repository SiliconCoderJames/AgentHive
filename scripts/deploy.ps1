# Deploy the workbench to %LOCALAPPDATA%\AgentHive and create a desktop shortcut.
# Survives repo/build directory cleanup.
#
# NOTE: for releases prefer the MSI (release/AgentHive-<ver>-x64.msi), which installs to the
# same directory and also registers an uninstaller. This script remains for a quick
# local deployment straight from a build tree.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1 -BuildDir build/msvc-release
#
# Source priority: an installed stage dir (_stage, produced by scripts/package.ps1) if present,
# otherwise <BuildDir>\src\gui\Release. Previously this path was hardcoded to build\full, which
# the documented build command (cmake -B build) never produces.
param(
    [string]$BuildDir = "build/full",
    [switch]$FromStage
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$src = $null
if ($FromStage -or (Test-Path (Join-Path $root "_stage\agenthive.exe"))) {
    $src = Join-Path $root "_stage"
} else {
    $src = Join-Path $root (Join-Path $BuildDir "src\gui\Release")
}
if (-not (Test-Path (Join-Path $src "agenthive.exe"))) {
    Write-Error "agenthive.exe not found in $src`n  build first: cmake --build $BuildDir --config Release`n  or install the stage dir: cmake --install $BuildDir --config Release --prefix _stage --component Runtime"
    exit 1
}
Write-Host "SRC=$src"

$dest = Join-Path $env:LOCALAPPDATA "AgentHive"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
# /MIR keeps the deployed tree exactly in sync (removes stale Qt DLLs)
robocopy $src $dest /MIR /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { Write-Error "robocopy failed: $LASTEXITCODE"; exit 1 }

# 桌面快捷方式：AgentHive 命名 + 应用图标；并清理历史品牌的旧快捷方式
$desktop = [Environment]::GetFolderPath("Desktop")
$lnkPath = Join-Path $desktop "AgentHive.lnk"
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($lnkPath)
$lnk.TargetPath = Join-Path $dest "agenthive.exe"
$lnk.WorkingDirectory = $dest
$lnk.IconLocation = (Join-Path $dest "agenthive.exe") + ",0"
$lnk.Description = "AgentHive - local-first collaboration hub for AI agents"
$lnk.Save()
foreach ($old in @("ZCode Platform.lnk", "ZCode 协作平台.lnk")) {
    $oldPath = Join-Path $desktop $old
    if (Test-Path $oldPath) { Remove-Item $oldPath -Force }
}

# 清理历史品牌部署目录（仅含程序副本，不含用户数据；被占用时跳过留待下次）
$legacy = Join-Path $env:LOCALAPPDATA "ZCodePlatform"
if (Test-Path $legacy) {
    try {
        Remove-Item $legacy -Recurse -Force -ErrorAction Stop
        Write-Host "LEGACY_DIR_REMOVED=$legacy"
    } catch {
        Write-Host "LEGACY_DIR_BUSY=$legacy"
    }
}

Write-Host "DEPLOY_DIR=$dest"
Write-Host "LNK=$lnkPath"
