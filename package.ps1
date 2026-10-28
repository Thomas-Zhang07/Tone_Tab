# Tone_Tab packaging script (called by package.bat).
# Output:
#   release\Tone_Tab-v<version>.zip
#     standalone app + assets + presets/settings + docs + licenses + source
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw -Encoding UTF8
$ver = if ($cmake -match 'project\(Tone_Tab VERSION ([0-9.]+)') { $Matches[1] } else { '0.0.0' }

$outDir  = Join-Path $root 'release'
$name    = "Tone_Tab-v$ver"
$stage   = Join-Path $outDir $name
$tmp     = Join-Path $outDir '_tmp'
$zipFile = Join-Path $outDir "$name.zip"

foreach ($p in @($stage, $tmp, $zipFile)) { if (Test-Path $p) { Remove-Item $p -Recurse -Force } }
New-Item -ItemType Directory -Force $stage | Out-Null
New-Item -ItemType Directory -Force $tmp | Out-Null

Write-Host "[1/4] Copying app, plug-in and tone assets..."
Copy-Item (Join-Path $root 'Tone_Tab.exe') $stage

$vst3 = Join-Path $root 'build\Tone_TabPlugin_artefacts\Release\VST3\Tone_Tab.vst3'
if (Test-Path $vst3) {
    New-Item -ItemType Directory -Force (Join-Path $stage 'VST3') | Out-Null
    Copy-Item $vst3 (Join-Path $stage 'VST3') -Recurse
    Write-Host "        VST3: VST3\Tone_Tab.vst3"
} else {
    Write-Host "  note: VST3 not found (build with TONE_BUILD_PLUGIN=ON)" -ForegroundColor Yellow
}

$assets = Join-Path $root 'assets'
if (Test-Path $assets) { Copy-Item $assets (Join-Path $stage 'assets') -Recurse }
else { New-Item -ItemType Directory (Join-Path $stage 'assets') | Out-Null }

$gvDir = Join-Path $assets 'gervana'
if (Test-Path $gvDir) {
    $gvCount = (Get-ChildItem $gvDir -Include *.png,*.jpg -Recurse -File).Count
    if ($gvCount -lt 6) {
        Write-Host "  note: assets\gervana has only $gvCount artwork files (6 expected)" -ForegroundColor Yellow
    }
}

Write-Host "      Bundling presets and settings..."
$myPresets = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Tone_Tab\presets'
if (Test-Path $myPresets) {
    Copy-Item $myPresets (Join-Path $stage 'presets') -Recurse
    Write-Host ("        presets: {0}" -f (Get-ChildItem (Join-Path $stage 'presets') -Filter *.xml).Count)
}

$mySettings = Join-Path $env:APPDATA 'Tone_Tab\settings.xml'
if (Test-Path $mySettings) {
    [xml]$x = Get-Content $mySettings -Raw -Encoding UTF8
    $drop = @('audioDevice', 'guitarChannel', 'knownPlugins', 'tabRecent', 'tabLastFile', 'tabLastFolder',
              'tabWatchFolder', 'judgeCalibrationMs', 'defaultPresetsVersion', 'bundledPresetsStamp')
    foreach ($v in @($x.PROPERTIES.VALUE)) { if ($drop -contains $v.GetAttribute('name')) { [void]$x.PROPERTIES.RemoveChild($v) } }
    $setDir = New-Item -ItemType Directory (Join-Path $stage 'defaults')
    $x.Save((Join-Path $setDir.FullName 'settings.xml'))
    $cur = @($x.PROPERTIES.VALUE | Where-Object { $_.GetAttribute('name') -eq 'currentPreset' })
    if ($cur.Count -gt 0) { Write-Host ("        startup preset: {0}" -f $cur[0].GetAttribute('val')) }
}

$scores = New-Item -ItemType Directory (Join-Path $stage 'scores')
Set-Content (Join-Path $scores.FullName 'PUT_SCORES_HERE.txt') -Encoding UTF8 -Value @(
    'Put Guitar Pro files (.gp3 .gp4 .gp5 .gpx .gp) in this folder.',
    'The practice view opens here by default. You can also drag score files onto the window.')

Copy-Item (Join-Path $root 'licenses') (Join-Path $stage 'licenses') -Recurse
Copy-Item (Join-Path $root 'README.md') $stage
$docs = Join-Path $root 'docs'
if (Test-Path $docs) { Copy-Item $docs (Join-Path $stage 'docs') -Recurse }

Write-Host "[2/4] Bundling source (AGPLv3)..."
$src = Join-Path $tmp 'Tone_Tab-source'
New-Item -ItemType Directory $src | Out-Null
foreach ($item in @('Source', 'tests', 'tools', 'licenses', 'docs', 'CMakeLists.txt', 'CMakePresets.json',
                    'build.bat', 'package.bat', 'package.ps1', 'README.md', 'PLAN.md',
                    'NAMING.md', 'LICENSE', '.gitignore')) {
    $p = Join-Path $root $item
    if (Test-Path $p) { Copy-Item $p (Join-Path $src $item) -Recurse }
}
New-Item -ItemType Directory (Join-Path $src 'third_party') | Out-Null
foreach ($z in @('JUCE-9.0.4.zip', 'NAM-core.zip')) {
    $p = Join-Path $root "third_party\$z"
    if (Test-Path $p) { Copy-Item $p (Join-Path $src 'third_party') }
}
[IO.Compression.ZipFile]::CreateFromDirectory($src, (Join-Path $stage 'source.zip'),
    [IO.Compression.CompressionLevel]::Optimal, $true, [Text.Encoding]::UTF8)

Write-Host "[3/4] Creating archive..."
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zipFile,
    [IO.Compression.CompressionLevel]::Optimal, $true, [Text.Encoding]::UTF8)

Write-Host "[4/4] Cleaning temporary files..."
Remove-Item $tmp -Recurse -Force

$mb = [math]::Round((Get-Item $zipFile).Length / 1MB, 1)
Write-Host ""
Write-Host "Created: $zipFile ($mb MB)"
Write-Host "Staged:  $stage"
