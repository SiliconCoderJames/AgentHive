<#
.SYNOPSIS
  Build the AgentHive Windows release artifacts: portable ZIP + MSI + SHA256SUMS.

.DESCRIPTION
  Single entry point for local packaging and CI. Steps:
    1. configure + build (skippable with -SkipBuild)
    2. cmake --install to a clean stage dir (this is where windeployqt puts the Qt runtime)
    3. assert the stage really is runnable (exe, Qt DLLs, platform plugin, MSVC runtime)
    4. generate the WiX file manifest from the stage dir
    5. build the MSI with WiX (pinned version, see -WixVersion)
    6. zip the stage dir as the portable build
    7. write SHA256SUMS.txt

  Everything lands in release/ (overridable with -OutDir).

  NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads UTF-8-without-BOM as ANSI,
  which corrupts non-ASCII comments and can break parsing.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version 1.0.0

.EXAMPLE
  # CI: version comes from the git tag
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version $env:GITHUB_REF_NAME
#>
param(
    [string]$Version = "1.0.0",
    [string]$BuildDir = "build/full",
    [string]$QtPrefix = "C:/Qt/6.8.3/msvc2022_64",
    [string]$Generator = "Visual Studio 18 2026",
    [string]$Configuration = "Release",
    [string]$OutDir = "release",
    [string]$StageDir = "_stage",
    [string]$WixVersion = "6.0.2",
    [switch]$SkipBuild,
    [switch]$PerMachine
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Step($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }

# ---- 版本号：允许 v1.2.3 / 1.2.3-rc1；MSI 需要纯 x.y.z ----
$verRaw = $Version -replace '^v', ''
if ($verRaw -notmatch '^([0-9]+)\.([0-9]+)\.([0-9]+)') {
    throw "version must look like 1.2.3 (got '$Version')"
}
$verNumeric = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
Write-Host "Release version: $Version (MSI: $verNumeric)"

Step "1/7 configure + build"
if (-not $SkipBuild) {
    # Pass the version as ONE quoted argument: PowerShell 5.1 truncates an unquoted
    # -DAGENTHIVE_VERSION=1.0.0 down to "...=1", which would silently stamp the wrong
    # version into the UI, /api/health and the installer.
    cmake -S . -B $BuildDir -G $Generator -A x64 "-DCMAKE_PREFIX_PATH=$QtPrefix" `
        "-DAGENTHIVE_VERSION=$Version" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    cmake --build $BuildDir --config $Configuration | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
} else {
    Write-Host "skipped (-SkipBuild)"
}

Step "2/7 install to stage dir ($StageDir)"
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
cmake --install $BuildDir --config $Configuration --prefix "$root/$StageDir" --component Runtime | Out-Host
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

Step "3/7 assert the staged build is runnable"
# These are exactly the files whose absence used to make a hand-assembled package fail to
# start on a clean machine. Fail loudly instead of publishing a broken artifact.
$required = @(
    "zworkbench.exe",
    "agent-cli.exe",
    "platformd.exe",
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Svg.dll",
    "platforms/qwindows.dll",
    "licenses/THIRD-PARTY-NOTICES.md", "licenses/LICENSE"
)
$missing = @()
foreach ($f in $required) {
    if (-not (Test-Path (Join-Path $StageDir $f))) { $missing += $f }
}
if ($missing.Count -gt 0) { throw "stage dir is incomplete, missing: $($missing -join ', ')" }
foreach ($f in $required) { Write-Host ("  OK  {0}" -f $f) }

# MSVC runtime: windeployqt --compiler-runtime only copies it when VCINSTALLDIR/VCToolsRedistDir
# is visible. Without it the app cannot start on a machine that lacks the VC++ Redistributable.
if (-not (Test-Path (Join-Path $StageDir "vcruntime140.dll"))) {
    throw @"
vcruntime140.dll is missing from the stage dir, so this package would NOT start on a clean
Windows install. Fix by running the build from a Visual Studio developer environment
(VCToolsRedistDir set) or by installing the VC++ Redistributable as a prerequisite.
Do not publish this artifact as-is.
"@
}
Write-Host "  OK  vcruntime140.dll"

Step "4/7 generate WiX file manifest"
if ($PerMachine) {
    & "$PSScriptRoot/gen-wix-files.ps1" -StageDir (Join-Path $root $StageDir) `
        -OutFile (Join-Path $root "$OutDir/wix/files.generated.wxs") -PerMachine
} else {
    & "$PSScriptRoot/gen-wix-files.ps1" -StageDir (Join-Path $root $StageDir) `
        -OutFile (Join-Path $root "$OutDir/wix/files.generated.wxs")
}

Step "5/7 build MSI (WiX $WixVersion)"
# WiX v7 requires accepting the Open Source Maintenance Fee EULA; v6 is the last release
# without it, which is why the version is pinned here.
$env:PATH = "$env:PATH;$env:USERPROFILE\.dotnet\tools"
if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    Write-Host "installing WiX $WixVersion as a dotnet global tool..."
    dotnet tool install --global wix --version $WixVersion | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "failed to install WiX $WixVersion" }
}
if ($PerMachine) { $scope = "perMachine"; $pm = 1 } else { $scope = "perUser"; $pm = 0 }
$msi = Join-Path $OutDir "AgentHive-$verNumeric-x64.msi"
# Adding an already-present extension reports a non-zero exit code; that is not a failure.
wix extension add -g "WixToolset.Util.wixext/$WixVersion" 2>&1 | Out-Host
if ($LASTEXITCODE -ne 0) { Write-Host "(extension already installed, continuing)" }
wix build (Join-Path $OutDir "wix/AgentHive.wxs") (Join-Path $OutDir "wix/files.generated.wxs") `
    -arch x64 -ext WixToolset.Util.wixext `
    -d Version="$verNumeric" -d Scope="$scope" -d PerMachine=$pm `
    -d IconFile="$root/src/gui/icon.ico" `
    -o "$root/$msi" | Out-Host
if ($LASTEXITCODE -ne 0) { throw "wix build failed" }

# ICE validation gate: catches real installer defects before they ship. Suppressed ICEs:
#   ICE91 - "per-user directory does not vary with ALLUSERS": expected for a per-user
#           package and harmless (we never use ALLUSERS=1).
Write-Host "validating MSI (ICE)..."
wix msi validate "$root/$msi" -ext WixToolset.Util.wixext -sice ICE91 | Out-Host
if ($LASTEXITCODE -ne 0) { throw "MSI failed ICE validation - see the errors above" }
Write-Host "  ICE validation passed"

Step "6/7 build portable ZIP"
$zipName = "AgentHive-$verNumeric-win64-portable.zip"
$zipPath = Join-Path $OutDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
# Top-level folder inside the zip so extracting does not scatter files.
$staging = Join-Path $env:TEMP ("agenthive-zip-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$inner = Join-Path $staging "AgentHive-$verNumeric"
New-Item -ItemType Directory -Force -Path $inner | Out-Null
Copy-Item -Path (Join-Path $StageDir "*") -Destination $inner -Recurse -Force
Compress-Archive -Path $inner -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item -Recurse -Force $staging

Step "7/7 checksums"
$sums = Join-Path $OutDir "SHA256SUMS.txt"
$lines = @()
foreach ($f in @($msi, $zipPath)) {
    $h = (Get-FileHash -Algorithm SHA256 (Join-Path $root $f)).Hash.ToLower()
    $lines += "$h  $(Split-Path -Leaf $f)"
}
[System.IO.File]::WriteAllLines((Join-Path $root $sums), $lines, (New-Object System.Text.UTF8Encoding($false)))
$lines | ForEach-Object { Write-Host "  $_" }

Write-Host "`n=== artifacts ===" -ForegroundColor Green
Get-ChildItem $OutDir -File | Where-Object { $_.Extension -in @(".msi", ".zip", ".txt") } |
    Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 2) } } | Format-Table -AutoSize | Out-Host

# Native commands above may leave a non-zero $LASTEXITCODE even on success
# (e.g. "wix extension add" for an already-installed extension); reaching here means
# every checked step passed, so report success explicitly.
exit 0
