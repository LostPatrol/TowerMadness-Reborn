# Start the original-code prototype or run one bounded verification scenario.
param([ValidateSet('play','scene','input','combat','map')][string]$Mode = 'play')
$ErrorActionPreference = 'Stop'
# The prototype never uses the product executable, release assets, or user profile.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$experimentDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse'
$executable = Join-Path $experimentDirectory 'build/Release/TowerMadnessX86Probe.exe'
$package = Join-Path $experimentDirectory 'package'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $experimentDirectory "$Mode-$timestamp.log"
$stdout = Join-Path $experimentDirectory "$Mode-$timestamp-stdout.log"
$stderr = Join-Path $experimentDirectory "$Mode-$timestamp-stderr.log"
if (!(Test-Path -LiteralPath $executable)) { throw 'Run tools/x86-reuse/prepare.ps1 and build.ps1 first.' }
if ($Mode -eq 'play') {
    Write-Output 'Original PlainPlane: click the field and original pie menu to build; Space sends a wave; close the window to exit.'
    Write-Output 'This silent prototype has no progress save or outer phone menu. Its portrait window is fixed at 480x800.'
}
# Only the expressly interactive mode opens a visible Windows window.
$style = if ($Mode -eq 'play') { 'Normal' } else { 'Hidden' }
$arguments = @(('"' + $package + '"'), $Mode, ('"' + $log + '"'))
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle $style -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit()
$result = [pscustomobject]@{ mode=$Mode; nativeExitCode=$process.ExitCode; log=$log; stdout=$stdout; stderr=$stderr }
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $experimentDirectory "$Mode-$timestamp-run.json") -Encoding utf8
$result | Format-List
if ($process.ExitCode -ne 0) { throw "The original-code probe failed with native exit $($process.ExitCode); inspect $log" }
