# Start the original-code prototype or run one bounded verification scenario.
param(
    [ValidateSet('play','scene','input','combat','upgrade','flood','autoplay','menu','menuauto','campaign','levels','map')][string]$Mode = 'play',
    [string]$Map = 'PlainPlane',
    [string]$OutputDirectory = '',
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
# The prototype never uses the product executable, release assets, or user profile.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$experimentDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse'
# Evidence goes to the requested directory; the historical default is preserved.
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = $experimentDirectory }
else { $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory) }
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { $buildDirectory = Join-Path $experimentDirectory 'build' }
else { $buildDirectory = [IO.Path]::GetFullPath($BuildDirectory) }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
# Each run keeps its own log, trace, and PNG directory so evidence is never overwritten.
$runDirectory = Join-Path $OutputDirectory "$Mode-$timestamp"
New-Item -ItemType Directory -Force $runDirectory | Out-Null
$executable = Join-Path $buildDirectory 'Release/TowerMadnessX86Probe.exe'
$package = Join-Path $experimentDirectory 'package'
$log = Join-Path $runDirectory "$Mode-$timestamp.log"
$stdout = Join-Path $runDirectory "$Mode-$timestamp-stdout.log"
$stderr = Join-Path $runDirectory "$Mode-$timestamp-stderr.log"
if (!(Test-Path -LiteralPath $executable)) { throw 'Run tools/x86-reuse/prepare.ps1 and build.ps1 first.' }
if ($Mode -eq 'play') {
    Write-Output 'Original PlainPlane: click the field and original pie menu to build; Space sends a wave; the bottom-left original Menu hot zone toggles a host pause; close the window to exit.'
    Write-Output 'This direct-play mode uses the host pause and does not persist progress. Use -Mode menu for the original menu. The silent portrait window is fixed at 480x800.'
}
if ($Mode -eq 'menu') {
    Write-Output 'Original MENU object: real main menu / level select / in-game pause. Escape returns from a running game or level select. Close the window to exit.'
}
# Only the expressly interactive modes open a visible Windows window.
$style = if ($Mode -eq 'play' -or $Mode -eq 'menu') { 'Normal' } else { 'Hidden' }
# campaign is a bounded GUI acceptance run, not a map selection: it maps to the host's
# existing bounded menuauto:campaign operation; duration and hidden rendering are host-owned.
# Every other mode keeps the historical mode:map operation.
$operation = if ($Mode -eq 'campaign') { 'menuauto:campaign' } elseif ([string]::IsNullOrWhiteSpace($Map)) { $Mode } else { $Mode + ':' + $Map }
$arguments = @(('"' + $package + '"'), $operation, ('"' + $log + '"'))
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle $style -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$process.WaitForExit()
$result = [pscustomobject]@{ mode=$Mode; map=$Map; operation=$operation; nativeExitCode=$process.ExitCode; runDirectory=$runDirectory; log=$log; stdout=$stdout; stderr=$stderr }
if ($Mode -eq 'campaign') {
    # The real selected map is the original game's own canStartMapWithName: call, never -Map.
    $selectedMap = $null
    if (Test-Path -LiteralPath $log) {
        $mapLine = Select-String -LiteralPath $log -Pattern 'PLATFORM_STORE canStartMapWithName: ' | Select-Object -First 1 -ExpandProperty Line
        if ($mapLine -and $mapLine -match 'canStartMapWithName: (.+?) -> 1') { $selectedMap = $Matches[1] }
    }
    $result | Add-Member -NotePropertyName scenario -NotePropertyValue 'campaign'
    $result | Add-Member -NotePropertyName mapKnob -NotePropertyValue $Map
    $result | Add-Member -NotePropertyName selectedMap -NotePropertyValue $selectedMap
    $result | Add-Member -NotePropertyName mapClaim -NotePropertyValue 'selectedMap comes from the original canStartMapWithName: call; map/mapKnob is a test knob, not a verified map'
}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory "$Mode-$timestamp-run.json") -Encoding utf8
$result | Format-List
if ($process.ExitCode -ne 0) { throw "The original-code probe failed with native exit $($process.ExitCode); inspect $log" }
