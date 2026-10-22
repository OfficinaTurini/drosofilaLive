param()
$ErrorActionPreference = "Stop"
function Fail([string]$m) { Write-Host "FAIL: $m" -ForegroundColor Red; exit 1 }
function D($v) { [double]::Parse([string]$v,[Globalization.CultureInfo]::InvariantCulture) }

$work = Split-Path -Parent $PSScriptRoot
$archivePath = Join-Path $work "archives\v017_causal_surprise.dro"
$reportPath = Join-Path $work "reports\v017_causal_surprise.html"
$csvPath = Join-Path $work "results\v017_causal_surprise_causal_surprise.csv"

if (!(Test-Path $archivePath)) { Fail "Missing archive: $archivePath" }
if (!(Test-Path $reportPath)) { Fail "Missing Causal Surprise report: $reportPath" }
if (!(Test-Path $csvPath)) { Fail "Missing Causal Surprise CSV: $csvPath" }

$html = [IO.File]::ReadAllText($reportPath, [Text.Encoding]::UTF8)
if ($html -notmatch "Causal Surprise") { Fail "Report does not identify Causal Surprise analysis" }
if ($html -notmatch "Structural percentile") { Fail "Report is missing structural percentile scatter" }
if ($html -notmatch "causal percentile") { Fail "Report is missing causal percentile description" }
$badEmDash = ([string][char]0x00E2) + [char]0x0080 + [char]0x0094
$badTimes  = ([string][char]0x00C3) + [char]0x00D7
$badDelta  = ([string][char]0x00CE) + [char]0x0094
if ($html.Contains($badEmDash) -or $html.Contains($badTimes) -or $html.Contains($badDelta)) { Fail "Report contains mojibake / encoding artifacts" }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    foreach ($required in @("manifest.json","explorer.json","discovery.json","provenance.json","causal_surprise.json")) {
        if ($names -notcontains $required) { Fail "Archive missing $required" }
    }

    function Read-EntryJson([string]$name) {
        $entry = $zip.GetEntry($name)
        if ($null -eq $entry) { Fail "Cannot find $name" }
        $reader = New-Object IO.StreamReader($entry.Open(), [Text.Encoding]::UTF8)
        try { return ($reader.ReadToEnd() | ConvertFrom-Json) }
        finally { $reader.Dispose() }
    }

    $manifest = Read-EntryJson "manifest.json"
    if ([int]$manifest.formatVersion -ne 3) { Fail "Expected .dro formatVersion 3" }
    if (-not [bool]$manifest.causalSurprisePresent) { Fail "Manifest does not advertise causalSurprisePresent" }

    $result = (Read-EntryJson "causal_surprise.json").result
    if (-not [bool]$result.valid) { Fail "Stored Causal Surprise result is invalid" }
    $entries = @($result.entries)
    if ($entries.Count -lt 1) { Fail "Stored Causal Surprise result has no entries" }
    if ([int]$result.validationRows -ne $entries.Count) { Fail "validationRows does not match stored entry count" }
    if ([int]$result.gatePassedRows -lt 1) { Fail "Smoke test has no gate-passed rows" }

    $ranked = @($entries | Where-Object { [bool]$_.gatePassed } | Sort-Object {[int]$_.surpriseRank})
    for ($i=0; $i -lt $ranked.Count; $i++) {
        $e = $ranked[$i]
        if ([int]$e.surpriseRank -ne ($i+1)) { Fail "Surprise rank sequence is not contiguous" }
        $expected = (D $e.causalPercentile) - (D $e.structuralPercentile)
        if ([math]::Abs((D $e.surpriseScore) - $expected) -gt 1e-10) {
            Fail "Surprise formula mismatch at root $($e.rootId)"
        }
        if ([int]$e.causalRank -lt 1) { Fail "Validated causal rank missing at root $($e.rootId)" }
    }
}
finally { $zip.Dispose() }

$csvRows = @(Import-Csv $csvPath)
if ($csvRows.Count -lt 1) { Fail "Causal Surprise CSV has no rows" }
if ($csvRows.Count -lt 1) { Fail "No CSV entries" }

Write-Host "PASS: v0.17 Causal Surprise report, .dro v3 persistence, formula and CSV export are coherent." -ForegroundColor Green
exit 0
