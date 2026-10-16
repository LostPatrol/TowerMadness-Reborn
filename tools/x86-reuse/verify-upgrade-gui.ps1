# Verify original weapon GUI click scripts against a selected native build.
param(
    [string]$Executable = 'agent/codex/x86-reuse/current/Release/TowerMadnessX86Probe.exe',
    [string]$OutputDirectory = 'agent/codex/x86-reuse/gui-upgrades',
    [string]$ClickScript = 'tools/x86-reuse/upgrade-clicks.json',
    [string]$ExpectedExecutableSha256 = ''
)
$ErrorActionPreference = 'Stop'
# Relative paths resolve from the project root; absolute overrides remain absolute.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$executablePath = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Executable)) { $Executable } else { Join-Path $projectDirectory $Executable }))
$baseDirectory = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $projectDirectory $OutputDirectory }))
$runDirectory = Join-Path $baseDirectory ('runs/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$package = Join-Path $projectDirectory 'agent/codex/x86-reuse/package'
$scriptPath = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($ClickScript)) { $ClickScript } else { Join-Path $projectDirectory $ClickScript }))
$executableHash = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash
if ($ExpectedExecutableSha256 -and $executableHash -ne $ExpectedExecutableSha256) { throw 'Requested executable hash mismatch.' }
# Bind the selected build and its recorded inputs without requiring a historical candidate.
$manifestPath = Join-Path (Split-Path (Split-Path $executablePath -Parent) -Parent) 'build-manifest.json'
$manifestHash = $null
if (Test-Path -LiteralPath $manifestPath) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.executableSha256 -ne $executableHash) { throw 'Executable does not match its build manifest.' }
    foreach ($inputFile in $manifest.inputs) {
        if ((Get-FileHash -LiteralPath $inputFile.path -Algorithm SHA256).Hash -ne $inputFile.sha256) { throw "Build input hash mismatch: $($inputFile.path)" }
    }
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
}
New-Item -ItemType Directory -Force $runDirectory | Out-Null
if ($manifestHash) { Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $runDirectory 'build-manifest.json') }
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $runDirectory 'runner.ps1')
Copy-Item -LiteralPath $scriptPath -Destination (Join-Path $runDirectory 'input-clicks.json')
$frozenScript = Join-Path $runDirectory 'input-clicks.json'
$configHash = (Get-FileHash -LiteralPath $frozenScript -Algorithm SHA256).Hash
$log = Join-Path $runDirectory 'native.log'
$stdout = Join-Path $runDirectory 'stdout.log'
$stderr = Join-Path $runDirectory 'stderr.log'
$previousAudio = $env:TOWERMADNESS_X86_AUDIO
$previousScript = $env:TOWERMADNESS_GUI_SCRIPT
try {
    $env:TOWERMADNESS_X86_AUDIO = '0'
    $env:TOWERMADNESS_GUI_SCRIPT = $frozenScript
    $process = Start-Process -FilePath $executablePath -ArgumentList @(('"' + $package + '"'), 'guiweapons:Peach', ('"' + $log + '"')) -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
} finally {
    $env:TOWERMADNESS_X86_AUDIO = $previousAudio
    $env:TOWERMADNESS_GUI_SCRIPT = $previousScript
}
$null = $process.Handle
$process.WaitForExit()
$failures = @()
$logText = Get-Content -LiteralPath $log -Raw
$diagnostics = (Get-Content -LiteralPath $stdout -Raw) + (Get-Content -LiteralPath $stderr -Raw) + $logText
$glErrors = [regex]::Matches($diagnostics, 'GL error after [^\r\n]+').Count
if ($process.ExitCode -ne 0) { $failures += "Native exit $($process.ExitCode)" }
if ($glErrors -ne 0) { $failures += "GL errors $glErrors" }
if ((Get-FileHash -LiteralPath $executablePath).Hash -ne $executableHash) { $failures += 'Executable changed' }
if ((Get-FileHash -LiteralPath $frozenScript).Hash -ne $configHash) { $failures += 'Config changed' }
$script = @(Get-Content -LiteralPath $frozenScript -Raw | ConvertFrom-Json)
$result = Get-Content -LiteralPath (Join-Path $runDirectory 'gui-result.json') -Raw | ConvertFrom-Json
if ($result.steps.Count -ne $script.Count -or $logText -notmatch ('GUI_WEAPONS_DONE steps=' + $script.Count)) { $failures += 'Incomplete GUI sequence' }
# Original readers must show an untouched pre-wave game at every click.
foreach ($step in $result.steps) {
    if ($step.state.lives -ne 10 -or $step.state.score -ne 0 -or $step.state.gameStarted) { $failures += "Unexpected player state at $($step.name)" }
}
[ordered]@{
    executable = $executablePath; executableSha256 = $executableHash; configSha256 = $configHash
    buildManifestSha256 = $manifestHash; expectedExecutableSha256 = $ExpectedExecutableSha256
    nativeExitCode = $process.ExitCode; glErrorCount = $glErrors; expectedSteps = $script.Count
    observedSteps = $result.steps.Count; acceptancePassed = ($failures.Count -eq 0); failures = $failures
    evidenceDirectory = $runDirectory
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runDirectory 'acceptance.json')
Get-Content -LiteralPath (Join-Path $runDirectory 'acceptance.json')
if ($failures.Count) { throw ($failures -join '; ') }
