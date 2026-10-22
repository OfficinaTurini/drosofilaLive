param()
$ErrorActionPreference = "Stop"
function Fail([string]$m) { Write-Host "FAIL: $m"; exit 1 }
$work = Split-Path -Parent $PSScriptRoot
$hitsPath = Join-Path $work "results\v0154_layered_depth3_hits.csv"
$htmlPath = Join-Path $work "reports\v0154_layered_depth3.html"
if (!(Test-Path $hitsPath)) { Fail "Missing $hitsPath" }
if (!(Test-Path $htmlPath)) { Fail "Missing $htmlPath" }
$rows=@(Import-Csv $hitsPath)
if ($rows.Count -lt 20) { Fail "Layered screen unexpectedly small: $($rows.Count) rows" }
$depths=@($rows | Select-Object -ExpandProperty graph_depth -Unique)
foreach($d in 1,2,3) { if ($depths -notcontains [string]$d) { Fail "Depth $d missing from layered candidate pool" } }
$cb=@($rows | Where-Object { $_.root_id -eq "720575940631340815" })
if ($cb.Count -ne 1) { Fail "CB3892b was not preserved by layered selection" }
if ([int]$cb[0].graph_rank -le 20) { Write-Host "NOTE: CB3892b global graph rank is now $($cb[0].graph_rank), still preserved." }
Write-Host "PASS: layered selection contains depths 1/2/3 and preserves CB3892b."
exit 0
