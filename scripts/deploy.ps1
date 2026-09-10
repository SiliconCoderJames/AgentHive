# Deploy the workbench to %LOCALAPPDATA%\AgentHive and create a desktop
# shortcut. Survives repo/build directory cleanup.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "build\full\src\gui\Release"
if (-not (Test-Path (Join-Path $src "zworkbench.exe"))) {
    Write-Error "zworkbench.exe not found -- build first: cmake --build build/full --config Release"
    exit 1
}

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
$lnk.TargetPath = Join-Path $dest "zworkbench.exe"
$lnk.WorkingDirectory = $dest
$lnk.IconLocation = (Join-Path $dest "zworkbench.exe") + ",0"
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
