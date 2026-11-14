<#
.SYNOPSIS
    Add extra NAM captures to the local Tone_Tab library.

.DESCRIPTION
    Copies .nam files into <assets>\nam\<Collection>\<pack>\ and appends their
    catalog entries to <assets>\nam\index.json. Files are de-duplicated by
    SHA-256, so running the script again with the same captures is a no-op.

    This is a complement to import_assets.ps1: that script rebuilds the bundled
    community / tone3000_a2 library from the big source folders, while this one
    keeps user-downloaded captures (TONE3000 etc.) separate under "custom".

    The source may be a folder containing .nam files directly, or a folder of
    subfolders (one per pack). A .nam at any depth is imported; the first folder
    level below -Source becomes the pack name.

.PARAMETER Source
    Folder that contains the .nam files (recursive).

.PARAMETER Destination
    Assets folder. Default: <repo>\assets.

.PARAMETER Collection
    Library collection folder/name. Default: custom.

.EXAMPLE
    tools\import_custom_nam.ps1 -Source E:\guitar_audio\_incoming_nam_20261114
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Source,
    [string]$Destination,
    [string]$Collection = 'custom'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $repo 'assets' }

if (-not (Test-Path -LiteralPath $Source)) {
    throw "Source folder not found: $Source"
}

$namDir = Join-Path $Destination 'nam'
$indexFile = Join-Path $namDir 'index.json'
$collectionDir = Join-Path $namDir $Collection
New-Item -ItemType Directory -Force -Path $collectionDir | Out-Null

$utf8 = New-Object Text.UTF8Encoding($false)
$sourceFull = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\')

$existing = @()
if (Test-Path -LiteralPath $indexFile) {
    $existing = [IO.File]::ReadAllText($indexFile, [Text.Encoding]::UTF8) | ConvertFrom-Json
}

$entries = New-Object System.Collections.ArrayList
$knownHashes = @{}
foreach ($e in @($existing)) {
    [void]$entries.Add($e)
    if ($e.sha256) { $knownHashes[[string]$e.sha256] = $true }
}

function Get-StrOrNull($value) {
    if ($null -eq $value -or "$value" -eq '') { return $null }
    return [string]$value
}

$files = Get-ChildItem -LiteralPath $sourceFull -Recurse -File -Filter *.nam |
         Sort-Object FullName
$added = 0
$skipped = 0

foreach ($f in $files) {
    $hash = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash

    $rel = $f.FullName.Substring($sourceFull.Length).TrimStart('\')
    $parts = $rel -split '\\'
    $pack = if ($parts.Count -gt 1) { $parts[0] } else { 'imported' }

    $packDir = Join-Path $collectionDir $pack
    New-Item -ItemType Directory -Force -Path $packDir | Out-Null
    $destFile = Join-Path $packDir $f.Name
    $relDest = ($Collection + '/' + $pack + '/' + $f.Name).Replace('\', '/')

    if ($knownHashes.ContainsKey($hash)) {
        # Already catalogued. Remove a stray copy with a different path only when
        # the canonical entry points somewhere else; keep the existing file.
        $canonical = $entries | Where-Object { $_.sha256 -eq $hash } | Select-Object -First 1
        if (-not (Test-Path -LiteralPath (Join-Path $namDir $canonical.file))) {
            Copy-Item -LiteralPath $f.FullName -Destination $destFile -Force
            $canonical.file = $relDest
            $canonical.collection = $Collection
        }
        ++$skipped
        continue
    }

    $doc = $null
    try {
        $doc = [IO.File]::ReadAllText($f.FullName, [Text.Encoding]::UTF8) | ConvertFrom-Json
    } catch {
        Write-Warning ("Skipping unreadable NAM file: {0} ({1})" -f $rel, $_.Exception.Message)
        continue
    }

    Copy-Item -LiteralPath $f.FullName -Destination $destFile -Force

    $meta = $doc.metadata
    $arch = Get-StrOrNull $doc.architecture
    if (-not $arch) { $arch = 'WaveNet' }
    $gearType = Get-StrOrNull $meta.gear_type
    $toneType = Get-StrOrNull $meta.tone_type

    $tags = New-Object System.Collections.ArrayList
    $isPedal = $gearType -match '(?i)pedal'
    $hasCab  = $gearType -match '(?i)cab|rig'

    # "A2" is the browser's full-rig filter, so only amps with a cab get it.
    if ($arch -eq 'SlimmableContainer' -and -not $isPedal) { [void]$tags.Add('a2') }

    if ($isPedal) {
        [void]$tags.Add('pedal')
        [void]$tags.Add('boost')
        if ($toneType -match '(?i)clean') { [void]$tags.Add('clean') }
    } else {
        if ($hasCab) { [void]$tags.Add('full_rig') }
        elseif ($gearType) { [void]$tags.Add('amp_only') }

        if ($toneType -match '(?i)clean') { [void]$tags.Add('clean') }
        elseif ($toneType -match '(?i)high|metal|gain') { [void]$tags.Add('high_gain') }
        elseif ($toneType) { [void]$tags.Add('crunch') }
    }

    # The entry schema matches tools/import_assets.ps1 so the browser reads both.
    $entry = [ordered]@{
        id            = 'nam_' + $hash.Substring(0, 12).ToLower()
        file          = $relDest
        sha256        = $hash
        bytes         = $f.Length
        collection    = $Collection
        architecture  = $arch
        formatVersion = Get-StrOrNull $doc.version
        metadata      = [ordered]@{
            name      = Get-StrOrNull $meta.name
            modeledBy = Get-StrOrNull $meta.modeled_by
            gearType  = $gearType
            gearMake  = Get-StrOrNull $meta.gear_make
            gearModel = Get-StrOrNull $meta.gear_model
            toneType  = $toneType
            loudness  = if ($null -ne $meta.loudness) { [double]$meta.loudness } else { $null }
        }
        tags          = @($tags | Sort-Object -Unique)
        aliases       = @()
    }

    [void]$entries.Add([pscustomobject]$entry)
    $knownHashes[$hash] = $true
    ++$added
    Write-Host ("  + {0}" -f $relDest)
}

$json = $entries | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText($indexFile, $json, $utf8)

Write-Host ''
Write-Host 'Custom NAM import complete'
Write-Host ("  added   : {0}" -f $added)
Write-Host ("  skipped : {0} (already catalogued)" -f $skipped)
Write-Host ("  index   : {0} entries" -f $entries.Count)
Write-Host ("  output  : {0}" -f $collectionDir)
