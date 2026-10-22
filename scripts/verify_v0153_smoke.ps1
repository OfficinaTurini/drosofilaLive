param([string]$Dir = "")
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($Dir)) {
    $Dir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\results"))
}
Write-Host "Results directory: $Dir"
$hitsPath = Join-Path $Dir "v0153_smoke_hits.csv"
$epiPath  = Join-Path $Dir "v0153_smoke_epistasis.csv"
if (!(Test-Path $hitsPath) -or !(Test-Path $epiPath)) { throw "Missing smoke CSV(s): run v0153_01_discovery_smoke.bas first." }
$hits = @(Import-Csv $hitsPath)
$epi  = @(Import-Csv $epiPath)
if ($hits.Count -ne 3) { Write-Error "FAIL: expected 3 screened hits rows, found $($hits.Count)."; exit 1 }
$validated = @($hits | Where-Object { $_.validation_valid -eq "1" })
if ($validated.Count -ne 2) { Write-Error "FAIL: expected 2 fully validated hits, found $($validated.Count)."; exit 1 }
foreach ($h in $validated) {
    if ([int]$h.validation_n -ne 2 -or [int]$h.validation_requested -ne 2) { Write-Error "FAIL: validation replicate count mismatch for root $($h.root_id)."; exit 1 }
}
if ($epi.Count -ne 1) { Write-Error "FAIL: expected exactly 1 epistasis pair, found $($epi.Count)."; exit 1 }
if ($epi[0].valid -ne "1" -or [int]$epi[0].n -ne 2 -or [int]$epi[0].requested -ne 2) { Write-Error "FAIL: smoke epistasis pair is incomplete."; exit 1 }
Write-Host "PASS: smoke CSV structure and replicate counts are coherent."
exit 0
