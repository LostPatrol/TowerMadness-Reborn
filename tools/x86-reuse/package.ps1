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
# Export the English player guide from the tracked README instead of duplicating it.
$englishReadme = Get-Content -LiteralPath (Join-Path $projectDirectory 'README.md') -Raw
$developerSection = $englishReadme.IndexOf('## For developers', [StringComparison]::Ordinal)
if ($developerSection -lt 0) { throw 'The English README is missing its developer section.' }
$englishPlayerGuide = $englishReadme.Substring(0, $developerSection) -replace '(?m)^\[English\]\(README.md\).*\r?\n', ''
$englishPlayerGuide.TrimEnd() | Set-Content -LiteralPath (Join-Path $OutputDirectory 'README.md') -Encoding utf8
Write-Output $OutputDirectory
