$ErrorActionPreference = "Stop"

$reports = Join-Path $PSScriptRoot "..\reports"
if (!(Test-Path $reports)) { throw "Reports directory not found: $reports" }

$report = Get-ChildItem $reports -Filter "reproduce_*.html" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$report) { throw "No v0.16.1 reproduction report found in $reports" }

$text = Get-Content $report.FullName -Raw
Write-Host "Checking $($report.FullName)"

if ($text -notmatch "Drosofila Live Reproduction Report") { throw "Not a REPRODUCE report" }
if ($text -notmatch "Required preflight mismatches:</b> 0") { throw "Preflight is not exact" }
if ($text -notmatch "Numeric mismatches:</b> 0") { throw "Numerical mismatches detected" }
if ($text -notmatch ">PASS<") { throw "PASS status not found" }

Write-Host "v0.16.1 REPRODUCE verification: PASS" -ForegroundColor Green
