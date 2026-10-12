# Package only runtime files and dependency notices into a dated Windows release.
param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$releaseRoot = Join-Path $projectRoot 'release'
if (!$OutputPath) {
    $stamp = [TimeZoneInfo]::ConvertTimeBySystemTimeZoneId([DateTime]::UtcNow,'China Standard Time').ToString('yyyyMMdd-HHmmss')
    $OutputPath = Join-Path $releaseRoot "TowerMadness-Reborn-Windows-x64-$stamp.zip"
}
# Keep process diagnostics, verification data, and prior archives outside the package.
$names = @('TowerMadness.exe','SDL3.dll','assets','docs','README.md','SDL3-LICENSE.txt','json-LICENSE.txt','STB-LICENSE.txt')
$files = foreach ($name in $names) {
    $path = Join-Path $releaseRoot $name
    if (!(Test-Path -LiteralPath $path)) { throw "Release file is missing: $name. Run tools/build.ps1." }
    $path
}
Compress-Archive -LiteralPath $files -DestinationPath $OutputPath -CompressionLevel Optimal
Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256
Write-Output $OutputPath
