param()
$ErrorActionPreference = "Stop"
function Fail([string]$m) { Write-Host "FAIL: $m"; exit 1 }
function D($v) { [double]::Parse([string]$v,[Globalization.CultureInfo]::InvariantCulture) }
$work = Split-Path -Parent $PSScriptRoot
$hitsPath = Join-Path $work "results\v0154_multiseed_smoke_hits.csv"
$htmlPath = Join-Path $work "reports\v0154_multiseed_smoke.html"
if (!(Test-Path $hitsPath)) { Fail "Missing $hitsPath" }
if (!(Test-Path $htmlPath)) { Fail "Missing $htmlPath" }
$rows=@(Import-Csv $hitsPath)
if ($rows.Count -ne 6) { Fail "Expected 6 hits, found $($rows.Count)" }
$rank=1; $prev=[double]::PositiveInfinity
foreach($r in $rows) {
  if ([int]$r.screen_requested -ne 3 -or [int]$r.screen_n -ne 3) { Fail "Screen N mismatch at root $($r.root_id)" }
  if ([int]$r.causal_rank -ne $rank) { Fail "Causal rank mismatch at row $rank" }
  $a=[math]::Abs((D $r.screen_delta_mean))
  if ($a -gt $prev + 1e-9) { Fail "|screen mean delta| is not non-increasing" }
  $prev=$a; $rank++
}
Write-Host "PASS: multi-seed screen uses 3/3 replicates, ranking is coherent, HTML report exists."
exit 0
