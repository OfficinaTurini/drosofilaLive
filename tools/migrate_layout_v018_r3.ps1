# Drosofila Live v0.18-r3 layout migration helper
# Consolidates ./docs -> ./help and ./data_bin -> ./data without overwriting existing files.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$help = Join-Path $root "help"
$data = Join-Path $root "data"
$docs = Join-Path $root "docs"
$dataBin = Join-Path $root "data_bin"

New-Item -ItemType Directory -Force -Path $help | Out-Null
New-Item -ItemType Directory -Force -Path $data | Out-Null

function Move-WithoutOverwrite([string]$sourceDir, [string]$destDir) {
    if (-not (Test-Path $sourceDir)) { return }
    Get-ChildItem -LiteralPath $sourceDir -Force | ForEach-Object {
        $dest = Join-Path $destDir $_.Name
        if (Test-Path $dest) {
            Write-Warning "Destination already exists, left source untouched: $dest"
        } else {
            Move-Item -LiteralPath $_.FullName -Destination $destDir
            Write-Host "Moved $($_.FullName) -> $destDir"
        }
    }
    if (-not (Get-ChildItem -LiteralPath $sourceDir -Force -ErrorAction SilentlyContinue)) {
        Remove-Item -LiteralPath $sourceDir -Force
    }
}

Move-WithoutOverwrite $docs $help
Move-WithoutOverwrite $dataBin $data

Write-Host ""
Write-Host "Canonical layout:"
Write-Host "  help\it, help\en-GB, help\credits.html, help\license.html"
Write-Host "  data\neurons.bin, data\edges.bin, data\annotations.tsv (optional)"
Write-Host "  data\stimuli, data\flywire783_circuit"
