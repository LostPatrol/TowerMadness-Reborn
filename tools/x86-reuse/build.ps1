# Build the isolated original-code experiment without rebuilding the product.
$ErrorActionPreference = 'Stop'
# Keep all generated executables and linker files inside the process directory.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$buildDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse/build'
& 'C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe' (Join-Path $PSScriptRoot 'gl-signatures.mjs')
if ($LASTEXITCODE -ne 0) { throw 'OpenGL signature preparation failed.' }
$cmake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake -S $PSScriptRoot -B $buildDirectory -A Win32
if ($LASTEXITCODE -ne 0) { throw 'The experiment configuration failed.' }
& $cmake --build $buildDirectory --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'The experiment compilation failed.' }
Write-Output (Join-Path $buildDirectory 'Release/TowerMadnessX86Probe.exe')
