# Deploy the workbench to %LOCALAPPDATA%\ZCodePlatform and create a desktop
# shortcut. Survives repo/build directory cleanup.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root "build\full\src\gui\Release"
if (-not (Test-Path (Join-Path $src "zworkbench.exe"))) {
    Write-Error "zworkbench.exe not found -- build first: cmake --build build/full --config Release"
    exit 1
}

$dest = Join-Path $env:LOCALAPPDATA "ZCodePlatform"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
# /MIR keeps the deployed tree exactly in sync (removes stale Qt DLLs)
robocopy $src $dest /MIR /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { Write-Error "robocopy failed: $LASTEXITCODE"; exit 1 }

$desktop = [Environment]::GetFolderPath("Desktop")
$lnkPath = Join-Path $desktop "ZCode Platform.lnk"
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($lnkPath)
$lnk.TargetPath = Join-Path $dest "zworkbench.exe"
$lnk.WorkingDirectory = $dest
$lnk.Description = "ZCode multi-agent collaboration workbench (local only, includes HTTP API)"
$lnk.Save()

Write-Host "DEPLOY_DIR=$dest"
Write-Host "LNK=$lnkPath"
