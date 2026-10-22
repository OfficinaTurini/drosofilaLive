param([string]$Dir = "")
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($Dir)) {
    $Dir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\results"))
}
Write-Host "Results directory: $Dir"
$hitsPath = Join-Path $Dir "v0153_reference_hits.csv"
$epiPath  = Join-Path $Dir "v0153_reference_epistasis.csv"
$expPath  = Join-Path $Dir "v0153_reference_explorer.csv"
if (!(Test-Path $hitsPath) -or !(Test-Path $epiPath) -or !(Test-Path $expPath)) { throw "Missing reference CSV(s): run v0153_02_end_to_end_reference.bas first." }
$explorer = @(Import-Csv $expPath)
$hits = @(Import-Csv $hitsPath)
$epi = @(Import-Csv $epiPath)
if ($explorer.Count -lt 10) { Write-Error "FAIL: Explorer returned fewer than 10 exported candidates."; exit 1 }
if ($hits.Count -ne 10) { Write-Error "FAIL: expected 10 screen rows, found $($hits.Count)."; exit 1 }
$validated = @($hits | Where-Object { $_.validation_valid -eq "1" })
if ($validated.Count -ne 3) { Write-Error "FAIL: expected 3 fully validated hits, found $($validated.Count)."; exit 1 }
foreach ($h in $validated) {
    if ([int]$h.validation_n -ne 20 -or [int]$h.validation_requested -ne 20) { Write-Error "FAIL: expected 20/20 validation replicates for root $($h.root_id)."; exit 1 }
}
if ($epi.Count -ne 3) { Write-Error "FAIL: Top-3 epistasis must yield 3 pairs; found $($epi.Count)."; exit 1 }
foreach ($e in $epi) {
    if ($e.valid -ne "1" -or [int]$e.n -ne 10 -or [int]$e.requested -ne 10) { Write-Error "FAIL: incomplete epistasis pair $($e.root_a) x $($e.root_b)."; exit 1 }
}
Write-Host "PASS: reference end-to-end CSV structure, Top-N cardinality and replicate counts are coherent."
exit 0
