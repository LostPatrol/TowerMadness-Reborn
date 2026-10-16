# Stage the native x86 port and original assets for personal offline Windows use.
param(
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
# Keep the portable output separate from original packages, saves, and earlier builds.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$experimentDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse'
if (!$BuildDirectory) { $BuildDirectory = Join-Path $experimentDirectory 'current' }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectDirectory ('release/TowerMadness-x86-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory; existing output is preserved.' }
$manifestPath = Join-Path $BuildDirectory 'build-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$executable = Join-Path $BuildDirectory 'Release/TowerMadnessX86Probe.exe'
if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash -ne $manifest.executableSha256) {
    throw 'The executable does not match its frozen build manifest.'
}
foreach ($inputFile in $manifest.inputs) {
    if ((Get-FileHash -LiteralPath $inputFile.path -Algorithm SHA256).Hash -ne $inputFile.sha256) {
        throw "Build input changed since compilation: $($inputFile.path)"
    }
}
$runtime = Join-Path $OutputDirectory 'runtime'
New-Item -ItemType Directory -Path (Join-Path $runtime 'package/lib') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'logs') -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $OutputDirectory 'TowerMadness.exe')
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $runtime 'build-manifest.json')
Copy-Item -LiteralPath (Join-Path $experimentDirectory 'package/lib/x86') -Destination (Join-Path $runtime 'package/lib') -Recurse
Copy-Item -LiteralPath (Join-Path $experimentDirectory 'package/assets') -Destination (Join-Path $runtime 'package') -Recurse
Copy-Item -LiteralPath (Join-Path $projectDirectory 'assets/textures') -Destination (Join-Path $runtime 'textures') -Recurse
foreach ($inputName in @('gs-instructions.json','gl-signatures.json')) {
    Copy-Item -LiteralPath (Join-Path $experimentDirectory $inputName) -Destination $runtime
}
# Record runtime content hashes without copying any development or user state.
$files = @(Get-ChildItem -LiteralPath $runtime -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path=$_.FullName.Substring($OutputDirectory.TrimEnd('\').Length + 1); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
[pscustomobject]@{ schema=1; executableSha256=$manifest.executableSha256; files=$files } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runtime 'portable-runtime.json') -Encoding utf8
$launcher = @'
@rem Native original-x86 Windows port; saves stay beside this launcher.
@echo off
setlocal
cd /d "%~dp0"
set "TOWERMADNESS_X86_AUDIO=1"
for %%A in (%*) do (
  if /i "%%~A"=="silent" set "TOWERMADNESS_X86_AUDIO=0"
  if /i "%%~A"=="1080" set "TOWERMADNESS_WINDOW=648x1080"
  if /i "%%~A"=="60" set "TOWERMADNESS_FPS=60"
  if /i "%%~A"=="120" set "TOWERMADNESS_FPS=120"
  if /i "%%~A"=="unlimited" set "TOWERMADNESS_FPS=0"
)
if not exist logs mkdir logs
"%~dp0TowerMadness.exe" "%~dp0runtime\package" menu "%~dp0logs\game.log"
if errorlevel 1 pause
endlocal
'@
$launcher | Set-Content -LiteralPath (Join-Path $OutputDirectory 'Play.cmd') -Encoding ascii
@'
# TowerMadness Windows x86 port

Personal learning and nostalgia build using the original game code and assets. Run Play.cmd on Windows 10/11; no emulator or development tools are needed. Optional arguments can be combined: Play.cmd silent 1080 120. Use silent to disable audio, 1080 for a 648x1080 client, and 60/120/unlimited for a frame cap.

Mouse: original menus and tower wheel. Drag: pan the camera. Wheel: zoom using original camera limits. Space: send wave. Esc: original pause/menu. F6: cycle 60/120/unlimited FPS; the title shows measured FPS. F5: save the current game. F9: restore that save. Closing the window does not save automatically; press F5 before quitting. Restarting loads the last F5 checkpoint into the original paused Resume menu. Saves and settings stay in data; logs stay in logs.

Resize or maximize the window freely. The original portrait layout is preserved with black borders; 3D renders at the actual viewport resolution while original texture detail stays unchanged. Default frame cap is 60; unlimited may increase GPU/CPU load and is still limited by the display/driver. Guide and Credits open the unchanged original offline HTML in your Windows browser. Windows music fading and distance attenuation approximate the original audio platform. Online services remain unavailable. This is a private development build, not a complete released port; do not redistribute it.
'@ | Set-Content -LiteralPath (Join-Path $OutputDirectory 'README.md') -Encoding utf8
Write-Output $OutputDirectory
