param([string]$Dir = "")
$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($Dir)) {
    $Dir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\results"))
}
Write-Host "Results directory: $Dir"
$a = Join-Path $Dir "v0153_repro_A_hits.csv"
$b = Join-Path $Dir "v0153_repro_B_hits.csv"
if (!(Test-Path $a) -or !(Test-Path $b)) { throw "Missing reproducibility CSV(s): run v0153_04_reproducibility_same_seed.bas first." }
$ha = (Get-FileHash -Algorithm SHA256 $a).Hash
$hb = (Get-FileHash -Algorithm SHA256 $b).Hash
Write-Host "A SHA256: $ha"
Write-Host "B SHA256: $hb"
if ($ha -ne $hb) { Write-Error "FAIL: same-seed hits CSVs differ."; exit 1 }
Write-Host "PASS: same-seed hits CSVs are byte-identical."
exit 0
