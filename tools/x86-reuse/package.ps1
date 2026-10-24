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
# Keep portable instructions in sync with the tracked Chinese README's player section.
$chineseReadme = Get-Content -LiteralPath (Join-Path $projectDirectory 'README.zh-CN.md') -Raw
$playerGuideEnd = $chineseReadme.IndexOf('<!-- player-guide:end -->', [StringComparison]::Ordinal)
if ($playerGuideEnd -lt 0) { throw 'The Chinese README is missing its player-guide end marker.' }
$playerGuide = $chineseReadme.Substring(0, $playerGuideEnd) -replace '(?m)^\[English\]\(README.md\).*\r?\n', ''
$playerGuide.TrimEnd() | Set-Content -LiteralPath (Join-Path $OutputDirectory '使用说明.md') -Encoding utf8
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
@rem Optional compatibility launcher; double-click TowerMadness.exe for ordinary play.
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
"%~dp0TowerMadness.exe"
if errorlevel 1 pause
endlocal
'@
$launcher | Set-Content -LiteralPath (Join-Path $OutputDirectory 'Play.cmd') -Encoding ascii
@'
# TowerMadness Windows x86 port

Personal learning and nostalgia build using the original game code and assets. Extract the entire ZIP into a writable folder, then double-click TowerMadness.exe on Windows 10/11. Do not run it from the ZIP preview. No command line, emulator or development tools are needed. Keep runtime beside the EXE. data contains local saves/settings; logs contains diagnostics. If startup fails, a dialog explains the error and log location. See 使用说明.md for the Chinese quick start and feedback instructions.

Mouse: menus and tower wheel. Drag: pan. Wheel: smooth zoom. Right click: close tower wheel. Space: pause/resume. N: next wave. W: upgrade selected tower. Q/E or middle drag: rotate camera; R: reset. Map arrows/A/D or wheel select maps (wheel up: previous; down: next); Map overview shows original thumbnails. F1: offline field guide. F10: PC settings and configurable keys. F11/Alt+Enter: borderless fullscreen. Window position/size/mode and F6 FPS cap are remembered. Original Options retains music/effects preferences.

F5 saves an atomic manual checkpoint with five older backups and success/failure feedback. F9 restores it. Exiting an active game offers Save/Discard/Cancel. Optional separate autosave is off by default; enable it in PC settings (two minutes), and use Restore separate autosave when needed. Restarting loads the last manual checkpoint into the original Resume menu. PC settings also opens the save folder and exports diagnostics into logs without account/save files.

To continue an earlier portable save: close both copies and copy the old data folder into this extracted folder. Preserve the old folder as a backup; never overwrite a newer save unintentionally. host-settings.json can be removed to reset window/FPS preferences without deleting game progress. Optional compatibility launcher arguments can be combined: Play.cmd silent 1080 120 (silent disables audio, 1080 requests 648x1080, 60/120/unlimited overrides the frame cap).

Default frame cap is 60; unlimited may increase GPU/CPU load and is still limited by the display/driver. Guide and Credits render original offline content inside the game. The trophy shows 36 original campaign achievements using local progress; official online metadata and other awards remain unavailable. Windows audio supports smooth music fades and stereo positioning. First resource loads may still produce long frames. If a problem occurs, copy logs/game.log before restarting, since the next launch replaces it. PC settings can export diagnostics; save files must be retained separately. This is a private personal build; do not redistribute it.
'@ | Set-Content -LiteralPath (Join-Path $OutputDirectory 'README.md') -Encoding utf8
Write-Output $OutputDirectory
