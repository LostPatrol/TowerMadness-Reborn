# Build the isolated original-code experiment without rebuilding the product.
param(
    [string]$BuildDirectory = '',
    # Absolute host.cpp to compile; the default is this directory's original source.
    # A driver restates it on every build so a stale CMake cache entry cannot decide
    # which source is compiled. A relative value resolves against this script, never the cwd.
    [string]$HostSource = '',
    # The driver signatures change only when SDL headers change; skip rewriting the shared file on repeat builds.
    [switch]$SkipGlSignatures,
    # Clean by default because copied source timestamps previously hid edits from MSBuild.
    [switch]$Incremental
)
$ErrorActionPreference = 'Stop'
# Keep all generated executables and linker files inside the requested directory.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { $buildDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse/current' }
else { $buildDirectory = [IO.Path]::GetFullPath($BuildDirectory) }
# Resolve the host translation unit to an absolute path without consulting the CMake cache.
if ([string]::IsNullOrWhiteSpace($HostSource)) { $hostSource = Join-Path $PSScriptRoot 'host.cpp' }
elseif ([IO.Path]::IsPathRooted($HostSource)) { $hostSource = [IO.Path]::GetFullPath($HostSource) }
else { $hostSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $HostSource)) }
if (!(Test-Path -LiteralPath $hostSource -PathType Leaf)) { throw "The host source was not found: $hostSource" }
# Compile an immutable input copy, then bind that copy and the executable in one manifest.
$inputDirectory = Join-Path $buildDirectory 'inputs'
New-Item -ItemType Directory -Force $inputDirectory | Out-Null
$compiledSource = Join-Path $inputDirectory 'host.cpp'
Copy-Item -LiteralPath $hostSource -Destination $compiledSource
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tower-restore.hpp') -Destination $inputDirectory
foreach ($audioInput in @('native-audio.hpp', 'original-audio.hpp', 'point-sprite-correction.hpp', 'weapon-combat.hpp', 'late-battle-probe.hpp')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $audioInput) -Destination $inputDirectory
}
$buildInputs = @($compiledSource, (Join-Path $inputDirectory 'tower-restore.hpp'), (Join-Path $inputDirectory 'native-audio.hpp'), (Join-Path $inputDirectory 'original-audio.hpp'), (Join-Path $inputDirectory 'point-sprite-correction.hpp'), (Join-Path $inputDirectory 'weapon-combat.hpp'), (Join-Path $inputDirectory 'late-battle-probe.hpp'), (Join-Path $projectDirectory 'third_party/stb_vorbis.c'), (Join-Path $projectDirectory 'third_party/json.hpp'), (Join-Path $PSScriptRoot 'CMakeLists.txt'))
$inputHashes = @($buildInputs | ForEach-Object { [pscustomobject]@{ path=$_; sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } })
$manifestPath = Join-Path $buildDirectory 'build-manifest.json'
if (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath }
if (!$SkipGlSignatures) {
    & 'C:/Users/LostPatrol_2/AppData/Local/hermes/node/node.exe' (Join-Path $PSScriptRoot 'gl-signatures.mjs')
    if ($LASTEXITCODE -ne 0) { throw 'OpenGL signature preparation failed.' }
}
$cmake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
# Always configure from this original script directory and restate the absolute source path.
& $cmake -S $PSScriptRoot -B $buildDirectory -A Win32 "-DX86_HOST_SOURCE=$compiledSource"
if ($LASTEXITCODE -ne 0) { throw 'The experiment configuration failed.' }
$buildArguments = @('--build', $buildDirectory, '--config', 'Release', '--parallel', '4')
if (!$Incremental) { $buildArguments += '--clean-first' }
& $cmake @buildArguments
if ($LASTEXITCODE -ne 0) { throw 'The experiment compilation failed.' }
foreach ($inputFile in $inputHashes) {
    if ((Get-FileHash -LiteralPath $inputFile.path -Algorithm SHA256).Hash -ne $inputFile.sha256) {
        throw "Build input changed during compilation: $($inputFile.path)"
    }
}
$executable = Join-Path $buildDirectory 'Release/TowerMadnessX86Probe.exe'
[pscustomobject]@{
    schema=1; builtAtUtc=[DateTime]::UtcNow.ToString('o'); sourcePath=$hostSource
    sourceSha256=$inputHashes[0].sha256; inputs=$inputHashes; executable=$executable
    executableSha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output $executable
