# End-to-end check of the in-app updater against a local fake release.
# ASCII-only on purpose (Windows PowerShell 5.1 misreads UTF-8-without-BOM scripts).
param(
    [string]$Exe  = "E:\MiderHive\build\full\src\gui\Release\miderhive.exe",
    [string]$Root = "E:\MiderHive\.upd-test",
    [int]$Port    = 19801,
    [string]$Mode = "badsig"   # badsig = correct download, wrong sha256 (verify must reject)
)
$ErrorActionPreference = "Stop"

# --- fake release content -------------------------------------------------
if (Test-Path $Root) { Remove-Item -Recurse -Force $Root }
New-Item -ItemType Directory -Force -Path $Root | Out-Null
$fake = [byte[]](1..4096 | ForEach-Object { [byte]($_ % 251) })
[System.IO.File]::WriteAllBytes("$Root\fake.msi", $fake)
$realHash = (Get-FileHash -Algorithm SHA256 "$Root\fake.msi").Hash.ToLower()
$sha = if ($Mode -eq "badsig") { "0" * 64 } else { $realHash }

# note: version must be NEWER than the running build for the prompt to appear
$manifest = @"
{
  "version": "1.0.1",
  "tag": "v1.0.1",
  "published_at": "2026-09-11T00:00:00Z",
  "repo": "local/test",
  "notes_url": "https://example.invalid/releases/tag/v1.0.1",
  "msi": { "name": "fake.msi", "url": "http://127.0.0.1:$Port/fake.msi",
           "sha256": "$sha", "size": $($fake.Length) },
  "portable": { "name": "fake.zip", "url": "http://127.0.0.1:$Port/fake.zip",
                "sha256": "$sha", "size": 4096 }
}
"@
[System.IO.File]::WriteAllText("$Root\latest.json", $manifest, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "MODE=$Mode  realHash=$($realHash.Substring(0,12))...  servedHash=$($sha.Substring(0,12))..."
Write-Host "ROOT=$Root  PORT=$Port"
