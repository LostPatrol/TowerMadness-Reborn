# Start the original-code prototype or run one bounded verification scenario.
param(
    [ValidateSet('play','scene','input','combat','upgrade','flood','autoplay','menu','menuauto','campaign','levels')][string]$Mode = 'play',
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
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { $buildDirectory = Join-Path $experimentDirectory 'current' }
else { $buildDirectory = [IO.Path]::GetFullPath($BuildDirectory) }
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
# Each run keeps its own log, trace, and PNG directory so evidence is never overwritten.
$runDirectory = Join-Path $OutputDirectory "$Mode-$timestamp"
New-Item -ItemType Directory -Force $runDirectory | Out-Null
$executable = Join-Path $buildDirectory 'Release/TowerMadnessX86Probe.exe'
$package = Join-Path $experimentDirectory 'package'
$log = Join-Path $runDirectory "$Mode-$timestamp.log"
$stdout = Join-Path $runDirectory "$Mode-$timestamp-stdout.log"
$stderr = Join-Path $runDirectory "$Mode-$timestamp-stderr.log"
$runnerHash = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $runDirectory 'runner.ps1')
if (!(Test-Path -LiteralPath $executable)) { throw 'Run tools/x86-reuse/prepare.ps1 and build.ps1 first.' }
# Hash before launch, then compare after exit so a concurrent rebuild cannot silently relabel a run.
$executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
$manifestPath = Join-Path $buildDirectory 'build-manifest.json'
$buildManifest = $null
if (Test-Path -LiteralPath $manifestPath) {
    $buildManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($buildManifest.executableSha256 -ne $executableHash) { throw 'Executable does not match its build manifest.' }
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $runDirectory 'build-manifest.json')
    New-Item -ItemType Directory -Force (Join-Path $runDirectory 'inputs') | Out-Null
    foreach ($inputName in @('host.cpp', 'tower-restore.hpp')) {
        Copy-Item -LiteralPath (Join-Path $buildDirectory "inputs/$inputName") -Destination (Join-Path $runDirectory 'inputs')
    }
}
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
# Touch Handle before waiting: without it .NET may not cache the exit code and $process.ExitCode reads empty.
$null = $process.Handle
$process.WaitForExit()
$hashAfter = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
$acceptance = @()
$lines = @(Get-Content -LiteralPath $log)
$logText = $lines -join "`n"
if ($null -eq $process.ExitCode) { $acceptance += 'Native exit code unavailable' }
if ($process.ExitCode -ne 0) { $acceptance += "Native exit $($process.ExitCode)" }
if ($hashAfter -ne $executableHash) { $acceptance += 'Executable changed during the run' }
if ($Mode -eq 'scene') {
    if (!(Test-Path -LiteralPath (Join-Path $runDirectory 'original-first-frame.png')) -or
        $logText -notmatch '(?m)^GAME_READY$' -or $logText -notmatch ('(?m)^GAMEMODE_READY ' + [regex]::Escape($Map) + '$')) {
        $acceptance += 'Original scene was not created and captured'
    }
}
if ($Mode -eq 'menuauto') {
    # Native exit 0 only means the loop returned; require each actual restore/pause leg as well.
    if ($Map -ieq 'continue') {
        if ($logText -notmatch 'ORIGINAL_SAVE_CONTINUE bytes=') { $acceptance += 'No saved game restored' }
    } else {
        foreach ($marker in @('ORIGINAL_TUTORIAL_DONE ', 'ORIGINAL_SAVE_HOTKEY ', 'ORIGINAL_LOAD_HOTKEY bytes=', 'ORIGINAL_SAVE file=', 'ORIGINAL_LOAD_IBS bytes=', 'ORIGINAL_MENU_PAUSE_FREEZE .*frozen=1')) {
            if ($logText -notmatch $marker) { $acceptance += "Missing $marker" }
        }
        if ($logText -notmatch 'ORIGINAL_SAVE_HOTKEY .*"gameStarted":true') { $acceptance += 'Save did not contain a started game' }
    }
    if ($Map -eq 'restorestress' -and @($lines | Where-Object { $_ -match '^ORIGINAL_LOAD_HOTKEY bytes=' }).Count -ne 13) {
        $acceptance += 'Expected 13 actual F9 restorations (initial + 12 repeats)'
    }
    $restored = $lines | Where-Object { $_ -match '^ORIGINAL_(LOAD_IBS|LOAD_HOTKEY|SAVE_CONTINUE) bytes=' } | Select-Object -Last 1
    $final = $lines | Where-Object { $_ -match '^ORIGINAL_MENU_FINAL ' } | Select-Object -Last 1
    if ($restored -and $final -and $restored -match '\{.*\}' ) {
        $restoredState = $Matches[0] | ConvertFrom-Json
        if ($final -match '\{.*\}') {
            $finalState = $Matches[0] | ConvertFrom-Json
            if (!$finalState.gameStarted -or $finalState.simulationMs -le $restoredState.simulationMs) { $acceptance += 'Restored game did not advance' }
        } else { $acceptance += 'Final game state unavailable' }
    } else { $acceptance += 'Restore/final observations unavailable' }
}
$result = [pscustomobject]@{ mode=$Mode; map=$Map; operation=$operation; nativeExitCode=$process.ExitCode; acceptancePassed=($acceptance.Count -eq 0); acceptanceErrors=$acceptance; executable=$executable; executableSha256=$executableHash; sourceSha256=$(if ($buildManifest) { $buildManifest.sourceSha256 } else { $null }); buildBound=($null -ne $buildManifest); runnerSha256=$runnerHash; runDirectory=$runDirectory; log=$log; stdout=$stdout; stderr=$stderr }
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
if ($acceptance.Count) { throw "Probe acceptance failed: $($acceptance -join '; '); inspect $log" }
