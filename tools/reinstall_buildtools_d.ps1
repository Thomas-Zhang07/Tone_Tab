# Stop the default-location Build Tools install and reinstall under D:\VSBuildTools.
# Run elevated. Logs to install_buildtools_d.log.

$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'install_buildtools_d.log'
"start $(Get-Date -Format o)" | Out-File -LiteralPath $log -Encoding utf8

Get-Process -Name vs_buildtools, vs_setup_bootstrapper, setup -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 5

New-Item -ItemType Directory -Force -Path 'D:\VSBuildTools\cache' | Out-Null

$exe = Join-Path $PSScriptRoot 'vs_buildtools.exe'
$arguments = @(
    '--quiet',
    '--wait',
    '--norestart',
    '--installPath', 'D:\VSBuildTools',
    '--cache', 'D:\VSBuildTools\cache',
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--includeRecommended'
)

& $exe @arguments *>&1 | Tee-Object -LiteralPath $log -Append
$code = $LASTEXITCODE
"exit $code" | Out-File -LiteralPath $log -Append
