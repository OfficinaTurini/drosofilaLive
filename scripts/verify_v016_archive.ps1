param()
$ErrorActionPreference = "Stop"
function Fail([string]$m) { Write-Host "FAIL: $m"; exit 1 }

$work = Split-Path -Parent $PSScriptRoot
$archivePath = Join-Path $work "archives\v016_discovery_roundtrip.dro"
if (!(Test-Path $archivePath)) { Fail "Missing $archivePath" }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $required = @("manifest.json","experiment.json","compare.json","epistasis.json","statistics.json","explorer.json","discovery.json","provenance.json")
    $names = @($zip.Entries | ForEach-Object { $_.FullName })
    foreach ($name in $required) {
        if ($names -notcontains $name) { Fail "Archive missing $name" }
    }

    function Read-EntryJson([string]$name) {
        $entry = $zip.GetEntry($name)
        if ($null -eq $entry) { Fail "Cannot find $name" }
        $reader = New-Object IO.StreamReader($entry.Open())
        try { return ($reader.ReadToEnd() | ConvertFrom-Json) }
        finally { $reader.Dispose() }
    }

    $manifest = Read-EntryJson "manifest.json"
    if ([int]$manifest.formatVersion -ne 3) { Fail "Expected .dro formatVersion 3, got $($manifest.formatVersion)" }
    if ([int]$manifest.discoveryEntries -lt 1) { Fail "Manifest reports no Discovery entries" }
    if (-not [bool]$manifest.explorerPresent) { Fail "Manifest reports no Explorer result" }
    if (-not [bool]$manifest.provenancePresent) { Fail "Manifest reports no provenance" }

    $explorer = (Read-EntryJson "explorer.json").result
    if (-not [bool]$explorer.valid) { Fail "Explorer result is not valid" }
    if (@($explorer.candidates).Count -lt 1) { Fail "Explorer candidates missing" }
    if (@($explorer.edges).Count -lt 1) { Fail "Explorer edges missing" }

    $discovery = (Read-EntryJson "discovery.json").result
    if (@($discovery.entries).Count -lt 1) { Fail "Discovery entries missing" }
    if ([int]$discovery.screenReplicatesRequested -ne 2) { Fail "Screen replicate count was not preserved" }
    if ([int]$discovery.validationReplicatesRequested -ne 3) { Fail "Validation replicate count was not preserved" }

    $prov = (Read-EntryJson "provenance.json").result
    if (-not [bool]$prov.valid) { Fail "Provenance is not valid" }
    if ([string]$prov.selectionMode -ne "GLOBAL") { Fail "Expected GLOBAL selection provenance" }
    if ([int]$prov.selectionTopN -ne 3) { Fail "Expected selectionTopN=3" }
    if (-not [bool]$prov.screenParametersValid) { Fail "Screen parameters missing" }
    if (-not [bool]$prov.validationParametersValid) { Fail "Validation parameters missing" }
    if (-not [bool]$prov.epistasisParametersValid) { Fail "Epistasis parameters missing" }
    if ([int]$prov.screenParameters.randomSeed -ne 12345) { Fail "Screen seed not preserved" }
    if ([int]$prov.validationParameters.randomSeed -ne 20000) { Fail "Validation seed not preserved" }
    if ([int]$prov.epistasisParameters.randomSeed -ne 30000) { Fail "Epistasis seed not preserved" }

    Write-Host "PASS: .dro v3 contains Explorer, Discovery and phase-specific scientific provenance."
}
finally {
    $zip.Dispose()
}

$expectedCsv = @(
    "results\v016_discovery_roundtrip_explorer.csv",
    "results\v016_discovery_roundtrip_discovery.csv",
    "results\v016_discovery_roundtrip_discovery_epistasis.csv",
    "results\v016_discovery_roundtrip_provenance.csv"
)
foreach ($rel in $expectedCsv) {
    $p = Join-Path $work $rel
    if (!(Test-Path $p)) { Fail "Missing exported v0.16 CSV: $p" }
}
Write-Host "PASS: v0.16 archive-specific CSV exports are present."
exit 0
