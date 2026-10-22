param(
    [string]$AppDir = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($AppDir)) {
    $ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
    $AppDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir "..\_app\drosofilaLive\x64"))
}

function Fail([string]$Message) {
    Write-Host "FAIL: $Message" -ForegroundColor Red
    exit 1
}

$Required = @(
    "help\it\index.html",
    "help\it\fondamenti.html",
    "help\en-GB\index.html",
    "help\en-GB\fundamentals.html",
    "help\style.css",
    "docs\license.html",
    "docs\credits.html"
)

foreach ($Rel in $Required) {
    $Path = Join-Path $AppDir $Rel
    if (!(Test-Path $Path)) { Fail "Missing deployed release document: $Path" }
    if ((Get-Item $Path).Length -lt 100) { Fail "Release document is unexpectedly small: $Path" }
}

$Credits = Get-Content -Raw -Encoding UTF8 (Join-Path $AppDir "docs\credits.html")
foreach ($Token in @("FlyWire Consortium", "FAFB", "10.1038/s41586-024-07558-y", "10.1016/j.cell.2018.06.019", "CC BY-NC 4.0")) {
    if ($Credits -notmatch [regex]::Escape($Token)) { Fail "Credits page missing required acknowledgement: $Token" }
}

$Italian = Get-Content -Raw -Encoding UTF8 (Join-Path $AppDir "help\it\index.html")
$English = Get-Content -Raw -Encoding UTF8 (Join-Path $AppDir "help\en-GB\index.html")
if ($Italian -notmatch "Cosa Drosofila Live NON dimostra") { Fail "Italian limitations section missing" }
if ($English -notmatch "What Drosofila Live does NOT prove") { Fail "English limitations section missing" }
if ($Italian -notmatch "Esperimento 07") { Fail "Italian guided experiment 07 missing" }
if ($English -notmatch "Experiment 07") { Fail "English guided experiment 07 missing" }

$FoundIT = Get-Content -Raw -Encoding UTF8 (Join-Path $AppDir "help\it\fondamenti.html")
$FoundEN = Get-Content -Raw -Encoding UTF8 (Join-Path $AppDir "help\en-GB\fundamentals.html")
foreach ($Token in @("Thomas Hunt Morgan", "FAFB", "FlyWire", "139.255", "54,5 milioni", "root ID", "Leaky Integrate-and-Fire", "ablazione computazionale", "epistasi computazionale")) {
    if ($FoundIT -notmatch [regex]::Escape($Token)) { Fail "Italian foundations missing required concept: $Token" }
}
foreach ($Token in @("Thomas Hunt Morgan", "FAFB", "FlyWire", "139,255", "54.5 million", "root ID", "Leaky Integrate-and-Fire", "computational ablation", "computational perturbation epistasis")) {
    if ($FoundEN -notmatch [regex]::Escape($Token)) { Fail "English foundations missing required concept: $Token" }
}
if ($Italian -notmatch "fondamenti.html") { Fail "Italian operational manual does not link to foundations" }
if ($English -notmatch "fundamentals.html") { Fail "English operational manual does not link to foundations" }

Write-Host "PASS: v0.18-r1 operational help, foundations, licence, credits, acknowledgements and guided experiments are deployed." -ForegroundColor Green
