# Build and assemble a standalone native Windows release without installing tools.
param([switch]$SkipAssets, [switch]$Incremental)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
# Prefer a PATH installation, then the existing Visual Studio bundled CMake.
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' }
if (!(Test-Path -LiteralPath $cmakePath)) { throw 'CMake is missing. Install Visual Studio C++ tools or add CMake to PATH.' }
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'third_party/json.hpp'))) { throw 'Project dependencies are missing. Run tools/setup-dependencies.ps1 after approving the download.' }
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'third_party/stb_vorbis.c'))) { throw 'The pinned Vorbis decoder is missing. Run tools/setup-dependencies.ps1.' }
if (!$SkipAssets) {
    if (Test-Path -LiteralPath (Join-Path $projectRoot 'agent/codex/apk/assets')) {
        & node (Join-Path $PSScriptRoot 'prepare-assets.mjs')
        if ($LASTEXITCODE -ne 0) { throw 'Asset preparation failed.' }
    } elseif (Test-Path -LiteralPath (Join-Path $projectRoot 'assets/manifest.json')) {
        Write-Output 'Using checked-in prepared assets; original APK assets are not present.'
    } else { throw 'Game assets are missing. Supply extracted original assets and run tools/prepare-assets.mjs.' }
}
& $cmakePath -S $projectRoot -B (Join-Path $projectRoot 'build') -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
# Tool-written files can retain timestamps older than objects; releases rebuild by default.
$buildArguments = @('--build',(Join-Path $projectRoot 'build'),'--config','Release','--parallel','4')
if (!$Incremental) { $buildArguments += '--clean-first' }
& $cmakePath @buildArguments
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
$releasePath = Join-Path $projectRoot 'release'
New-Item -ItemType Directory -Force $releasePath | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'build/Release/TowerMadness.exe'),(Join-Path $projectRoot 'build/Release/SDL3.dll') -Destination $releasePath
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets') -Destination $releasePath -Recurse -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/SDL3-3.4.18/LICENSE.txt') -Destination (Join-Path $releasePath 'SDL3-LICENSE.txt')
if (Test-Path -LiteralPath (Join-Path $projectRoot 'third_party/json-LICENSE.txt')) { Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/json-LICENSE.txt') -Destination $releasePath }
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $releasePath
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/STB-LICENSE.txt') -Destination $releasePath
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs') -Destination $releasePath -Recurse -Force
Write-Output "Built $releasePath/TowerMadness.exe"
