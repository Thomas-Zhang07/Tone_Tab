<#
.SYNOPSIS
    Build the Tone_Tab asset library from the NAM and otherproject collections.

.DESCRIPTION
    Copies and de-duplicates the NAM models, cabinet IRs, and Gervana artwork into
    <repo>\assets. Files are de-duplicated by SHA-256; the 24 "starter" captures and
    the 18 otherproject captures are byte-identical copies of community-pack files,
    so they are stored once and recorded as aliases/tags in index.json.

    The script is idempotent: re-running it overwrites the generated library.

.PARAMETER NamRoot
    Root containing community-pack, starter, tone3000-a2. Default D:\NAM\models.

.PARAMETER IrsRoot
    IR packs root. Default D:\NAM\irs.

.PARAMETER OtherProjectRoot
    The otherproject tone library folder (the one containing NAM\ and Gervana\).
    Auto-detected from E:\guitar_audio\otherproject\project when omitted.

.PARAMETER Destination
    Output assets folder. Default <repo>\assets.
#>
[CmdletBinding()]
param(
    [string]$NamRoot = 'D:\NAM\models',
    [string]$IrsRoot = 'D:\NAM\irs',
    [string]$OtherProjectRoot,
    [string]$Destination
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $repo 'assets' }

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
function Get-Sha([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-NamHeader([string]$Path) {
    # The metadata block is at the front of the JSON file; 128 KiB is plenty.
    $fs = [IO.File]::OpenRead($Path)
    try {
        $buf = New-Object byte[] 131072
        $n = $fs.Read($buf, 0, $buf.Length)
        [Text.Encoding]::UTF8.GetString($buf, 0, $n)
    } finally {
        $fs.Dispose()
    }
}

function Get-Str([string]$Text, [string]$Key) {
    $m = [regex]::Match($Text, '"' + [regex]::Escape($Key) + '"\s*:\s*"((?:[^"\\]|\\.)*)"')
    if ($m.Success) { return [regex]::Unescape($m.Groups[1].Value) }
    return $null
}

function Get-Num([string]$Text, [string]$Key) {
    $m = [regex]::Match($Text, '"' + [regex]::Escape($Key) + '"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?)')
    if ($m.Success) { return [double]::Parse($m.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) }
    return $null
}

function Copy-File([string]$Source, [string]$DestDir) {
    New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
    Copy-Item -LiteralPath $Source -Destination (Join-Path $DestDir ([IO.Path]::GetFileName($Source))) -Force
}

# ---------------------------------------------------------------------------
# locate sources
# ---------------------------------------------------------------------------
if (-not $OtherProjectRoot) {
    $candidates = Get-ChildItem -LiteralPath 'E:\guitar_audio\otherproject\project' -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'NAM') }
    if ($candidates) { $OtherProjectRoot = $candidates[0].FullName }
}
if (-not $OtherProjectRoot -or -not (Test-Path -LiteralPath $OtherProjectRoot)) {
    throw 'Could not locate the otherproject tone library (the folder containing NAM\).'
}

$sets = [ordered]@{
    'tone3000_a2'  = (Join-Path $NamRoot 'tone3000-a2')
    'community'    = (Join-Path $NamRoot 'community-pack')
    'starter'      = (Join-Path $NamRoot 'starter')
    'otherproject' = (Join-Path $OtherProjectRoot 'NAM')
}

$namDir     = Join-Path $Destination 'nam'
$irDir      = Join-Path $Destination 'ir'
$gervanaDir = Join-Path $Destination 'gervana'
$licDir     = Join-Path $namDir 'LICENSES'
foreach ($d in @($namDir, $irDir, $gervanaDir, $licDir)) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

# ---------------------------------------------------------------------------
# scan and de-duplicate
# ---------------------------------------------------------------------------
$script:canon   = @{}                          # sha256 -> canonical record
$script:aliases = New-Object System.Collections.ArrayList
$scanned = 0

function Add-Set([string]$Root, [string]$Collection) {
    if (-not (Test-Path -LiteralPath $Root)) {
        Write-Warning "missing source: $Root"
        return
    }
    $files = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter *.nam -ErrorAction SilentlyContinue
    foreach ($f in $files) {
        $script:scanned++
        $hash = Get-Sha $f.FullName
        $rel  = $f.FullName.Substring($Root.Length).TrimStart('\')

        if (-not $script:canon.ContainsKey($hash)) {
            $script:canon[$hash] = [pscustomobject]@{
                Hash       = $hash
                Path       = $f.FullName
                Collection = $Collection
                Rel        = $rel
                Name       = $f.Name
            }
        }

        if ($Collection -eq 'starter') {
            $tag = 'starter'
            if ($f.BaseName -match '^\d+_([a-z]+)_') {
                $kind = $Matches[1]
                if ($kind -eq 'highgain') { $tag = 'high_gain' }
                elseif ($kind -eq 'edge') { $tag = 'edge' }
                else { $tag = $kind }
            }
            $cab = 'emerald'
            if ($tag -eq 'high_gain') { $cab = 'brutal' }
            if ($tag -eq 'boost') { $cab = 'none' }
            [void]$script:aliases.Add([pscustomobject]@{
                Hash = $hash; Collection = 'starter'; Name = $f.BaseName
                Category = $tag; SuggestedIr = $cab
            })
        }
        elseif ($Collection -eq 'otherproject') {
            $category = $null
            if ($rel -match '^(\d)[-_]') {
                switch ([int]$Matches[1]) {
                    1 { $category = 'clean' }
                    2 { $category = 'crunch' }
                    3 { $category = 'high_gain' }
                    4 { $category = 'boost' }
                }
            }
            [void]$script:aliases.Add([pscustomobject]@{
                Hash = $hash; Collection = 'otherproject'; Name = $f.BaseName
                Category = $category; SuggestedIr = $null
            })
        }
    }
}

foreach ($k in $sets.Keys) { Add-Set $sets[$k] $k }

Write-Host ("Scanned {0} .nam files; {1} unique." -f $scanned, $script:canon.Count)

# ---------------------------------------------------------------------------
# copy canonical models
# ---------------------------------------------------------------------------
foreach ($c in $script:canon.Values) {
    $dest = Join-Path (Join-Path $namDir $c.Collection) $c.Name
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest) | Out-Null
    Copy-Item -LiteralPath $c.Path -Destination $dest -Force
}

# ---------------------------------------------------------------------------
# index.json
# ---------------------------------------------------------------------------
$entries = New-Object System.Collections.ArrayList
foreach ($c in ($script:canon.Values | Sort-Object Collection, Name)) {
    $head = Get-NamHeader $c.Path
    $arch = Get-Str $head 'architecture'
    if (-not $arch) { $arch = 'WaveNet' }

    $gearType = Get-Str $head 'gear_type'
    $tags = New-Object System.Collections.ArrayList
    if ($c.Collection -eq 'tone3000_a2') {
        [void]$tags.Add('a2'); [void]$tags.Add('full_rig')
    } else {
        [void]$tags.Add('a1')
    }
    if ($gearType) {
        if ($gearType -match 'cab|rig') { [void]$tags.Add('full_rig') }
        elseif ($gearType -match 'pedal') { [void]$tags.Add('pedal') }
        else { [void]$tags.Add('amp_only') }
    }
    elseif ($c.Collection -ne 'tone3000_a2') {
        # Older A1 captures usually have no gear_type. Infer it from the capture name.
        $modelName = Get-Str $head 'name'
        if ($modelName -and $modelName -match 'cab|full.?rig') { [void]$tags.Add('full_rig') }
        else { [void]$tags.Add('amp_only') }
    }

    $aliasList = New-Object System.Collections.ArrayList
    foreach ($a in ($script:aliases | Where-Object { $_.Hash -eq $c.Hash })) {
        if ($a.Category) { [void]$tags.Add($a.Category) }
        [void]$aliasList.Add([ordered]@{
            collection  = $a.Collection
            name        = $a.Name
            category    = $a.Category
            suggestedIr = $a.SuggestedIr
        })
    }

    # Promote to full_rig when the canonical name, embedded name, or a curated alias
    # says the capture includes a cabinet. The file itself is the source of truth only
    # for A2, which has gear_type.
    $nameBlob = $c.Name + ' ' + [string](Get-Str $head 'name')
    foreach ($a in $aliasList) { $nameBlob += ' ' + [string]$a.name }
    if ($nameBlob -match '(?i)cab|full[ _-]?rig') {
        if ($tags -contains 'amp_only') { [void]$tags.Remove('amp_only') }
        if (-not ($tags -contains 'full_rig')) { [void]$tags.Add('full_rig') }
    }

    $entry = [ordered]@{
        id            = 'nam_' + $c.Hash.Substring(0, 12).ToLower()
        file          = ($c.Collection + '/' + $c.Rel.Replace('\', '/'))
        sha256        = $c.Hash
        bytes         = (Get-Item -LiteralPath $c.Path).Length
        collection    = $c.Collection
        architecture  = $arch
        formatVersion = Get-Str $head 'version'
        metadata      = [ordered]@{
            name       = Get-Str $head 'name'
            modeledBy  = Get-Str $head 'modeled_by'
            gearType   = $gearType
            gearMake   = Get-Str $head 'gear_make'
            gearModel  = Get-Str $head 'gear_model'
            toneType   = Get-Str $head 'tone_type'
            loudness   = Get-Num $head 'loudness'
        }
        tags          = @($tags | Sort-Object -Unique)
        aliases       = @($aliasList)
    }
    [void]$entries.Add($entry)
}

$json = $entries | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText((Join-Path $namDir 'index.json'), $json, (New-Object Text.UTF8Encoding($false)))
Write-Host ("Wrote index.json with {0} entries." -f $entries.Count)

# ---------------------------------------------------------------------------
# IR packs
# ---------------------------------------------------------------------------
$irEntries = New-Object System.Collections.ArrayList
function Add-IrPack([string]$PackDir, [string]$PackId) {
    if (-not (Test-Path -LiteralPath $PackDir)) { Write-Warning "missing IR pack: $PackDir"; return }
    $files = Get-ChildItem -LiteralPath $PackDir -Recurse -File -ErrorAction SilentlyContinue
    foreach ($f in $files) {
        if ($f.Extension -ieq '.wav') {
            $rel = $f.FullName.Substring($PackDir.Length).TrimStart('\')
            $destDir = Join-Path (Join-Path $irDir $PackId) (Split-Path -Parent $rel)
            Copy-File $f.FullName $destDir
            $rate = $null
            if ($rel -match '44100|44\.1') { $rate = 44100 }
            elseif ($rel -match '48000|48k') { $rate = 48000 }
            [void]$irEntries.Add([ordered]@{
                file   = ('ir/' + $PackId + '/' + $rel.Replace('\', '/'))
                pack   = $PackId
                rate   = $rate
                sha256 = (Get-Sha $f.FullName)
                bytes  = $f.Length
            })
        }
        elseif ($f.Extension -ieq '.pdf') {
            Copy-File $f.FullName (Join-Path $irDir $PackId)
        }
        elseif ($f.Name -ieq 'README.txt') {
            Copy-File $f.FullName $irDir
        }
    }
}

Add-IrPack (Join-Path $IrsRoot 'jester-brutal')  'jester_brutal'
Add-IrPack (Join-Path $IrsRoot 'jester-emerald') 'jester_emerald'

$irReadme = Join-Path $IrsRoot 'README.txt'
if (Test-Path -LiteralPath $irReadme) {
    Copy-Item -LiteralPath $irReadme -Destination (Join-Path $irDir 'README.txt') -Force
}

$irJson = $irEntries | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText((Join-Path $irDir 'index.json'), $irJson, (New-Object Text.UTF8Encoding($false)))
Write-Host ("Wrote IR index with {0} impulses." -f $irEntries.Count)

# ---------------------------------------------------------------------------
# Gervana artwork
# ---------------------------------------------------------------------------
$gervanaSrc = Join-Path $OtherProjectRoot 'Gervana'
if (Test-Path -LiteralPath $gervanaSrc) {
    Get-ChildItem -LiteralPath $gervanaSrc -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -match '^\.(png|jpe?g)$' } |
        ForEach-Object { Copy-File $_.FullName $gervanaDir }
}
$gervanaNote = @'
Gervana effect artwork
======================

Drop a PNG or JPG here to replace the painted effect card / enlarged pedal.
File names must be:

  gv_wuyin.png     Wuyin fuzz
  gv_wuwu.png      Wuwu Plexi
  gv_bingzi.png    Bingzi overdrive
  gv_jiachen.png   Jiachen compressor / exciter
  gv_yiyou.png     Yiyou brown sound
  gv_yimao.png     Yimao AC30

Recommended: front-facing photo, transparent or solid background, aspect ratio
between 2:1 and 1:1, about 800 px wide. Restart the effect editor to see changes.
'@
[IO.File]::WriteAllText((Join-Path $gervanaDir 'PUT_IMAGES_HERE.txt'), $gervanaNote, (New-Object Text.UTF8Encoding($false)))

# ---------------------------------------------------------------------------
# licenses and notices
# ---------------------------------------------------------------------------
$communityCopying = Join-Path $sets['community'] 'COPYING'
if (Test-Path -LiteralPath $communityCopying) {
    Copy-Item -LiteralPath $communityCopying -Destination (Join-Path $licDir 'community-COPYING-GPL-3.0.txt') -Force
}
$tone3000Index = Join-Path $sets['tone3000_a2'] 'INDEX.txt'
if (Test-Path -LiteralPath $tone3000Index) {
    Copy-Item -LiteralPath $tone3000Index -Destination (Join-Path (Join-Path $namDir 'tone3000_a2') 'INDEX.txt') -Force
}
$otherCopying = Join-Path $sets['otherproject'] 'COPYING-GPLv3.txt'
if (Test-Path -LiteralPath $otherCopying) {
    Copy-Item -LiteralPath $otherCopying -Destination (Join-Path $licDir 'otherproject-COPYING-GPL-3.0.txt') -Force
}

$licenceReadme = @'
Model library licenses
======================

community/       pelennor2170/NAM_models, GPL-3.0. See community-COPYING-GPL-3.0.txt.
tone3000_a2/     Models downloaded from TONE3000. Per-model creator metadata is in
                 tone3000_a2/INDEX.txt and inside each .nam file. These captures are
                 NOT necessarily redistributable; check TONE3000's terms and each
                 creator's terms before publishing them in a repository or package.
starter/         24 curated files from the same GPL-3.0 community collection. They are
                 byte-identical to community/ files and are stored as aliases only.
otherproject/    18 captures bundled with LianqinHost, GPL-3.0. Byte-identical to
                 community/ files; stored as aliases only.

The app itself is AGPLv3. GPL-3.0 model files are distributed as separate works
alongside it; keep the notices and source availability.

Cab IR packs are documented in assets/ir/README.txt and the PDF handbooks.
'@
[IO.File]::WriteAllText((Join-Path $licDir 'README.md'), $licenceReadme, (New-Object Text.UTF8Encoding($false)))

$tone3000Notice = @'
TONE3000 capture notice
=======================

The .nam files in this folder were downloaded from https://www.tone3000.com by the
app's owner. They are full-rig (amp + cabinet) captures; do not add a cabinet IR.

Before committing this folder to a public repository or shipping it in a package,
confirm that TONE3000 and the individual creators permit redistribution. If they do
not, keep this folder local and let users import their own downloads with
tools\import_assets.ps1.
'@
[IO.File]::WriteAllText((Join-Path (Join-Path $namDir 'tone3000_a2') 'NOTICE.md'), $tone3000Notice, (New-Object Text.UTF8Encoding($false)))

# ---------------------------------------------------------------------------
# summary
# ---------------------------------------------------------------------------
$modelBytes = ($entries | ForEach-Object { $_.bytes } | Measure-Object -Sum).Sum
$irBytes = ($irEntries | ForEach-Object { $_.bytes } | Measure-Object -Sum).Sum
$aliasCount = $script:aliases.Count
Write-Host ''
Write-Host 'Tone_Tab asset import complete'
Write-Host ('  models : {0} unique ({1:N1} MB)' -f $entries.Count, ($modelBytes / 1MB))
Write-Host ('  aliases: {0} (starter + otherproject)' -f $aliasCount)
Write-Host ('  IRs    : {0} ({1:N1} MB)' -f $irEntries.Count, ($irBytes / 1MB))
Write-Host ('  output : {0}' -f $Destination)
