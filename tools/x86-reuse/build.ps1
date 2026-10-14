# Build the isolated original-code experiment without rebuilding the product.
param(
    [string]$BuildDirectory = '',
    # Absolute host.cpp to compile; the default is this directory's original source.
    # A driver restates it on every build so a stale CMake cache entry cannot decide
    # which source is compiled. A relative value resolves against this script, never the cwd.
    [string]$HostSource = '',
    # The driver signatures change only when SDL headers change; skip rewriting the shared file on repeat builds.
    [switch]$SkipGlSignatures
)
$ErrorActionPreference = 'Stop'
# Keep all generated executables and linker files inside the requested directory.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { $buildDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse/build' }
else { $buildDirectory = [IO.Path]::GetFullPath($BuildDirectory) }
# Resolve the host translation unit to an absolute path without consulting the CMake cache.
if ([string]::IsNullOrWhiteSpace($HostSource)) { $hostSource = Join-Path $PSScriptRoot 'host.cpp' }
elseif ([IO.Path]::IsPathRooted($HostSource)) { $hostSource = [IO.Path]::GetFullPath($HostSource) }
else { $hostSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $HostSource)) }
if (!(Test-Path -LiteralPath $hostSource -PathType Leaf)) { throw "The host source was not found: $hostSource" }
if (!$SkipGlSignatures) {
    & 'C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe' (Join-Path $PSScriptRoot 'gl-signatures.mjs')
    if ($LASTEXITCODE -ne 0) { throw 'OpenGL signature preparation failed.' }
}
$cmake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
# Always configure from this original script directory and restate the absolute source path.
& $cmake -S $PSScriptRoot -B $buildDirectory -A Win32 "-DX86_HOST_SOURCE=$hostSource"
if ($LASTEXITCODE -ne 0) { throw 'The experiment configuration failed.' }
& $cmake --build $buildDirectory --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'The experiment compilation failed.' }
Write-Output (Join-Path $buildDirectory 'Release/TowerMadnessX86Probe.exe')
