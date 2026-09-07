# 预取第三方依赖到 vendor/（源均为 GitHub，带重试，断网缓存后可离线构建）。
# 用法: powershell -ExecutionPolicy Bypass -File scripts\fetch-deps.ps1
# 注意：git 进度输出走 stderr，不能在 ErrorActionPreference=Stop 下用 2>&1 管道。
$root = Split-Path -Parent $PSScriptRoot
$vendor = Join-Path $root "vendor"
New-Item -ItemType Directory -Force -Path $vendor | Out-Null

$deps = @(
    @{ Name = "sqlite-vec";  Url = "https://github.com/asg017/sqlite-vec.git";    Tag = "v0.1.6" },
    @{ Name = "cpp-httplib"; Url = "https://github.com/yhirose/cpp-httplib.git";  Tag = "v0.16.3" },
    @{ Name = "json";        Url = "https://github.com/nlohmann/json.git";        Tag = "v3.11.3" }
)

foreach ($dep in $deps) {
    $dest = Join-Path $vendor $dep.Name
    if (Test-Path (Join-Path $dest ".git")) {
        Write-Host "[skip] $($dep.Name) already vendored"
        continue
    }
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    $ok = $false
    for ($i = 1; $i -le 5; $i++) {
        Write-Host "[clone $i/5] $($dep.Url) -> $dest"
        git clone --quiet --depth 1 --branch $dep.Tag $dep.Url $dest
        if ($LASTEXITCODE -eq 0) { $ok = $true; break }
        Write-Host "[retry] clone failed (exit $LASTEXITCODE), waiting..."
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        Start-Sleep -Seconds (10 * $i)
    }
    if (-not $ok) {
        Write-Error "failed to vendor $($dep.Name) after 5 attempts"
        exit 1
    }
}
Write-Host "vendor ready:"
Get-ChildItem $vendor | ForEach-Object { Write-Host "  $($_.Name)" }
