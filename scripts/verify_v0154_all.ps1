$ErrorActionPreference = "Stop"

$checks = @(
    "verify_v0154_multiseed.ps1",
    "verify_v0154_layered.ps1",
    "verify_v0154_gate_pipeline.ps1"
)

Write-Host "=== drosofilaLive v0.15.4 combined verifier ==="
foreach ($check in $checks) {
    $path = Join-Path $PSScriptRoot $check
    if (!(Test-Path $path)) {
        Write-Host "FAIL: missing verifier $path"
        exit 1
    }
    Write-Host ""
    Write-Host "--- $check ---"
    & powershell -NoProfile -ExecutionPolicy Bypass -File $path
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "FAIL: $check returned $LASTEXITCODE"
        exit $LASTEXITCODE
    }
}
Write-Host ""
Write-Host "PASS: all v0.15.4 verifier checks passed."
exit 0
