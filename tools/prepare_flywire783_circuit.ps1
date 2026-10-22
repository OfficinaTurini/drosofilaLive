param(
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectDir "data\flywire783_circuit"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

# Public FlyWire materialization-783 high-resolution precomputed skeleton source.
$BaseUrl = "https://flyem.mrc-lmb.cam.ac.uk/flyconnectome/flywire_skeletons_783"

$Neurons = @(
    [PSCustomObject]@{ Type="DNge031"; Root="720575940640589171"; Role="dynamic brake" },
    [PSCustomObject]@{ Type="CB0393";  Root="720575940632047890"; Role="driver / sustained" },
    [PSCustomObject]@{ Type="CB0177";  Root="720575940630579574"; Role="ignition driver" },
    [PSCustomObject]@{ Type="CB0248";  Root="720575940622695448"; Role="early modulator" },
    [PSCustomObject]@{ Type="CB0366";  Root="720575940614763666"; Role="initial modulator" },
    [PSCustomObject]@{ Type="DNge062"; Root="720575940627099338"; Role="late/output branch" },
    [PSCustomObject]@{ Type="CB0701";  Root="720575940660219265"; Role="TARGET/MN9" }
)

$Manifest = @()
Write-Host "Preparing FlyWire v783 causal-circuit geometry"
Write-Host "Output: $OutputDirectory"
Write-Host ""

foreach ($N in $Neurons) {
    $FileName = "$($N.Type)_$($N.Root).ngskel"
    $OutFile = Join-Path $OutputDirectory $FileName
    $Url = "$BaseUrl/$($N.Root)"

    Write-Host "[$($N.Type)] root $($N.Root)"
    Write-Host "  GET $Url"
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $OutFile

    $Bytes = [System.IO.File]::ReadAllBytes($OutFile)
    if ($Bytes.Length -lt 8) {
        throw "Downloaded file is too small: $OutFile"
    }

    $Nv = [BitConverter]::ToUInt32($Bytes, 0)
    $Ne = [BitConverter]::ToUInt32($Bytes, 4)
    $MinBytes = [uint64]8 + ([uint64]$Nv * 12) + ([uint64]$Ne * 8)
    if ($Nv -eq 0 -or [uint64]$Bytes.Length -lt $MinBytes) {
        throw "Invalid/truncated Neuroglancer skeleton: $OutFile (vertices=$Nv edges=$Ne bytes=$($Bytes.Length) minimum=$MinBytes)"
    }

    Write-Host "  OK: $Nv vertices, $Ne edges, $($Bytes.Length) bytes"
    $Manifest += [PSCustomObject]@{
        type = $N.Type
        role = $N.Role
        root_id = $N.Root
        file = $FileName
        vertices = $Nv
        edges = $Ne
        bytes = $Bytes.Length
        source = $Url
    }
}

$ManifestPath = Join-Path $OutputDirectory "manifest_v783.tsv"
$Manifest | Export-Csv -Delimiter "`t" -NoTypeInformation -Encoding UTF8 -Path $ManifestPath

Write-Host ""
Write-Host "DONE: 7/7 exact materialization-783 skeletons downloaded."
Write-Host "Manifest: $ManifestPath"
Write-Host "drosofilaLive can now use these files completely offline."
