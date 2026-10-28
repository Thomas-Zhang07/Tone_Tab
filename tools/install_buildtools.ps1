# Install the Visual Studio 2022 C++ build tools (MSVC + Windows SDK).
# Run elevated. Logs to tools\install_buildtools.log.

$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'install_buildtools.log'

"start $(Get-Date -Format o)" | Out-File -LiteralPath $log -Encoding utf8

$wingetArgs = @(
    'install',
    '--id', 'Microsoft.VisualStudio.2022.BuildTools',
    '--exact',
    '--silent',
    '--accept-package-agreements',
    '--accept-source-agreements',
    '--override', '--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
)

& winget @wingetArgs *>&1 | Tee-Object -LiteralPath $log -Append
$code = $LASTEXITCODE
"exit $code" | Out-File -LiteralPath $log -Append

Write-Host ''
if ($code -eq 0) {
    Write-Host 'Build Tools installed. You can close this window.'
} else {
    Write-Host "winget exited with code $code. See $log"
}
Start-Sleep -Seconds 20
