param()
$ErrorActionPreference = "Stop"
function Fail([string]$m) { Write-Host "FAIL: $m"; exit 1 }
function D($v) { [double]::Parse([string]$v,[Globalization.CultureInfo]::InvariantCulture) }
$work = Split-Path -Parent $PSScriptRoot
$hitsPath = Join-Path $work "results\v0154_confirmed_gate_pipeline_hits.csv"
$epiPath = Join-Path $work "results\v0154_confirmed_gate_pipeline_epistasis.csv"
$htmlPath = Join-Path $work "reports\v0154_confirmed_gate_pipeline.html"
if (!(Test-Path $hitsPath)) { Fail "Missing $hitsPath" }
if (!(Test-Path $epiPath)) { Fail "Missing $epiPath" }
if (!(Test-Path $htmlPath)) { Fail "Missing $htmlPath" }
$hits=@(Import-Csv $hitsPath)
$dn=@($hits | Where-Object { $_.root_id -eq "720575940627099338" })
$cb=@($hits | Where-Object { $_.root_id -eq "720575940631340815" })
if ($dn.Count -ne 1 -or $cb.Count -ne 1) { Fail "Confirmed roots missing from hit table" }
foreach($r in @($dn[0],$cb[0])) {
  if ([int]$r.validation_n -ne 100) { Fail "Expected validation N=100 for $($r.type)" }
  if ([int]$r.validation_ci95_excludes_zero -ne 1) { Fail "$($r.type) did not pass CI95 gate" }
}
$epi=@(Import-Csv $epiPath)
if ($epi.Count -ne 1) { Fail "Expected exactly one gated epistasis pair, found $($epi.Count)" }
if ([int]$epi[0].n -ne 100) { Fail "Expected epistasis N=100" }
Write-Host ("DNge062 mean Delta: {0:F3} Hz" -f (D $dn[0].validation_delta_mean))
Write-Host ("CB3892b mean Delta: {0:F3} Hz" -f (D $cb[0].validation_delta_mean))
Write-Host ("Interaction mean: {0:F3} Hz; CI95 [{1:F3}, {2:F3}]" -f (D $epi[0].interaction_mean),(D $epi[0].interaction_ci95_low),(D $epi[0].interaction_ci95_high))
Write-Host "PASS: CI95 gate admitted the two confirmed roots and generated exactly one 100-replicate pair."
exit 0
