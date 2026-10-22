param([string]$Dir = "")
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($Dir)) {
    $Dir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\results"))
}

Write-Host "=== drosofilaLive v0.15.3.2 acceptance checks ==="
Write-Host "Scripts directory: $PSScriptRoot"
Write-Host "Results directory: $Dir"

$checks = @(
    "verify_v0153_smoke.ps1",
    "verify_v0153_repro.ps1",
    "verify_v0153_reference.ps1"
)

foreach ($check in $checks) {
    Write-Host ""
    Write-Host "--- $check ---"
    & (Join-Path $PSScriptRoot $check) -Dir $Dir
    if ($LASTEXITCODE -ne 0) {
        Write-Error "FAIL: $check returned exit code $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "PASS: all v0.15.3.2 acceptance checks passed."
exit 0
