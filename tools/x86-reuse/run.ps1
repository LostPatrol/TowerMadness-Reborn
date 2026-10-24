# Start the original-code prototype or run one bounded verification scenario.
param(
    [ValidateSet('play','scene','input','combat','upgrade','flood','autoplay','menu','menuauto','campaign','levels','scores','speed','audio','weapons','guiweapons','weaponcombat')][string]$Mode = 'play',
    [string]$Map = 'PlainPlane',
    [string]$OutputDirectory = '',
    [string]$BuildDirectory = '',
    [ValidateSet('flame','mine','bait','slow','boost','rail','missile','nuke','hbomb','laser','plasma','mortar','shock','flak')][string]$CombatCase = 'flak',
    [switch]$Audio,
    [switch]$Silent
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
    foreach ($inputName in @('host.cpp', 'host-display.hpp', 'frame-pacing.hpp', 'desktop-launch.hpp', 'landscape-menu.hpp', 'high-price-probe.hpp', 'tower-restore.hpp', 'native-audio.hpp', 'original-audio.hpp', 'point-sprite-correction.hpp', 'weapon-combat.hpp', 'late-battle-probe.hpp', 'desktop-save.hpp', 'desktop-controls.hpp', 'desktop-map-menu.hpp', 'desktop-guide.hpp', 'desktop-ui.hpp', 'desktop-achievements.hpp', 'desktop-probe.hpp', 'towermadness.rc.in', 'towermadness.ico')) {
        if (!(Test-Path -LiteralPath (Join-Path $buildDirectory "inputs/$inputName"))) { continue }
        Copy-Item -LiteralPath (Join-Path $buildDirectory "inputs/$inputName") -Destination (Join-Path $runDirectory 'inputs')
    }
}
if ($Mode -eq 'play') {
    Write-Output 'Original PlainPlane: click the field and original pie menu to build; Space pauses, N sends a wave; F1 opens the field guide; F10 opens PC settings.'
    Write-Output 'This direct-play mode uses the host pause and does not persist progress. Use -Mode menu for the original menu. Resize/maximize the window; F6 selects 60/120/unlimited FPS. -Silent disables native audio.'
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
$previousAudio = $env:TOWERMADNESS_X86_AUDIO
$previousGuiScript = $env:TOWERMADNESS_GUI_SCRIPT
$previousCombatScript = $env:TOWERMADNESS_COMBAT_SCRIPT
$previousCombatCase = $env:TOWERMADNESS_COMBAT_CASE
try {
    if ($Mode -eq 'guiweapons') { $env:TOWERMADNESS_GUI_SCRIPT = Join-Path $PSScriptRoot 'weapon-clicks.json' }
    if ($Mode -eq 'weaponcombat') {
        $env:TOWERMADNESS_COMBAT_SCRIPT = Join-Path $PSScriptRoot 'weapon-combat-cases.json'
        $env:TOWERMADNESS_COMBAT_CASE = $CombatCase
    }
    $env:TOWERMADNESS_X86_AUDIO = if (!$Silent -and ($Audio -or $Mode -in @('play','menu'))) { '1' } else { '0' }
    $process = Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle $style -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
} finally {
    $env:TOWERMADNESS_X86_AUDIO = $previousAudio; $env:TOWERMADNESS_GUI_SCRIPT = $previousGuiScript
    $env:TOWERMADNESS_COMBAT_SCRIPT = $previousCombatScript; $env:TOWERMADNESS_COMBAT_CASE = $previousCombatCase
}
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
if ($Mode -eq 'scores' -and $logText -notmatch 'ORIGINAL_SCORE_ROUNDTRIP_DONE cycles=3') {
    $acceptance += 'Original score date/replay roundtrip did not complete'
}
if ($Mode -eq 'weapons' -and $logText -notmatch 'WEAPONS_DONE count=12') {
    $acceptance += 'Original weapon families did not finish their legal build/upgrade/sell chains'
}
if ($Mode -eq 'guiweapons' -and $logText -notmatch 'GUI_WEAPONS_DONE steps=97') {
    $acceptance += 'Original weapon wheel did not complete all twelve build/upgrade/sell click sequences'
}
if ($Mode -eq 'weaponcombat' -and $logText -notmatch ('WEAPON_COMBAT_PASSED case=' + [regex]::Escape($CombatCase) + ' expect=')) {
    $acceptance += 'The original weapon did not satisfy its actual combat trigger assertion'
}
if ($Mode -eq 'audio' -and $logText -notmatch 'ORIGINAL_AUDIO_DONE effects=5 music=2 remembered=.*musicIsolation=1 pendingCancellation=1') {
    $acceptance += 'Original sound pools, real playback, stop and music resume did not complete'
}
if ($Mode -eq 'speed' -and @($lines | Where-Object { $_ -match '^ORIGINAL_SPEED_SAMPLE ' }).Count -ne 4) {
    $acceptance += 'Expected four actual clock measurements for 1x/2x/4x/1x'
}
if ($Mode -eq 'menuauto' -and $Map -notin @('campaignfull','campaignall')) {
    # Native exit 0 only means the loop returned; require each actual restore/pause leg as well.
    if ($Map -eq 'presentcheck') {
        if ($logText -notmatch 'DESKTOP_PRESENT_DONE ') { $acceptance += 'Desktop presentation checks did not complete' }
    } elseif ($Map -eq 'motioncheck') {
        if ($logText -notmatch 'DESKTOP_MOTION_DONE ') { $acceptance += 'Desktop motion checks did not complete' }
    } elseif ($Map -eq 'desktopcheck') {
        if ($logText -notmatch 'DESKTOP_CHECK_DONE count=') { $acceptance += 'Desktop interaction checks did not complete' }
    } elseif ($Map.StartsWith('dlc-')) {
        if ($logText -notmatch ('ORIGINAL_DLC_GUI_DONE .*"map":"' + [regex]::Escape($Map.Substring(4)) + '"')) {
            $acceptance += 'Original DLC GUI did not start the requested map'
        }
    } elseif ($Map -eq 'help') {
        foreach ($page in @('guide.html', 'credits.html')) {
            if ($logText -notmatch ('PLATFORM_HELP page=' + [regex]::Escape($page) + ' launched=1')) {
                $acceptance += "Original $page was not launched by its menu action"
            }
        }
    } elseif ($Map -eq 'options') {
        foreach ($marker in @('ORIGINAL_OPTIONS_DONE ', 'PLATFORM_MUSIC volume=')) {
            if ($logText -notmatch $marker) { $acceptance += "Missing $marker" }
        }
    } elseif ($Map -eq 'latebattle') {
        foreach ($marker in @('ORIGINAL_SAVE_CONTINUE bytes=', 'ORIGINAL_GAME_FREED_BEFORE_RESTORE old=', 'ORIGINAL_LOAD_HOTKEY bytes=',
            'LATE_BATTLE_ACTIVE towers=11 ', 'LATE_BATTLE_RESTORED_EXACT ', 'LATE_BATTLE_RESTORE_ADVANCED ', 'LATE_BATTLE_DONE .*"won":true')) {
            if ($logText -notmatch $marker) { $acceptance += "Missing $marker" }
        }
    } elseif ($Map -ieq 'continue' -or $Map -eq 'continuehotkey') {
        if ($logText -notmatch 'ORIGINAL_SAVE_CONTINUE bytes=') { $acceptance += 'No saved game restored' }
    } else {
        foreach ($marker in @('ORIGINAL_TUTORIAL_DONE ', 'ORIGINAL_SAVE_HOTKEY ', 'ORIGINAL_LOAD_HOTKEY bytes=', 'ORIGINAL_SAVE file=', 'ORIGINAL_LOAD_IBS bytes=', 'ORIGINAL_MENU_PAUSE_FREEZE .*frozen=1', 'ORIGINAL_MENU_RESUME_STATE .*showing=0')) {
            if ($logText -notmatch $marker) { $acceptance += "Missing $marker" }
        }
        if ($logText -notmatch 'ORIGINAL_SAVE_HOTKEY .*"gameStarted":true') { $acceptance += 'Save did not contain a started game' }
    }
    if ($Map -eq 'restorestress' -and @($lines | Where-Object { $_ -match '^ORIGINAL_LOAD_HOTKEY bytes=' }).Count -ne 13) {
        $acceptance += 'Expected 13 actual F9 restorations (initial + 12 repeats)'
    }
    if ($Map -eq 'newgame' -and $logText -notmatch 'ORIGINAL_NEWGAME_GUI_DONE .*"gameStarted":false') {
        $acceptance += 'Original New Game GUI did not replace the restored active game'
    }
    $restored = $lines | Where-Object { $_ -match '^ORIGINAL_(LOAD_IBS|LOAD_HOTKEY|SAVE_CONTINUE) bytes=' } | Select-Object -Last 1
    $final = $lines | Where-Object { $_ -match '^ORIGINAL_MENU_FINAL ' } | Select-Object -Last 1
    if ($Map -in @('options','help','newgame','desktopcheck','motioncheck','presentcheck') -or $Map.StartsWith('dlc-')) {
        # Options has no GAME object; its actual setting changes are checked above.
    } elseif ($restored -and $final -and $restored -match '\{.*\}' ) {
        $restoredState = $Matches[0] | ConvertFrom-Json
        if ($final -match '\{.*\}') {
            $finalState = $Matches[0] | ConvertFrom-Json
            if (!$finalState.gameStarted -or $finalState.simulationMs -le $restoredState.simulationMs) { $acceptance += 'Restored game did not advance' }
        } else { $acceptance += 'Final game state unavailable' }
    } else { $acceptance += 'Restore/final observations unavailable' }
}
if ($Mode -eq 'campaign' -or ($Mode -eq 'menuauto' -and $Map -in @('campaignfull','campaignall'))) {
    # A clean native exit is insufficient: require the whole original victory and continuation chain.
    foreach ($marker in @('ORIGINAL_MENU_GAME_WON ', 'ORIGINAL_CAMPAIGN_ENDGAME_STABLE .*"won":true',
        'ORIGINAL_CAMPAIGN_NEXT_MAP .*"map":"DividedAttention"', 'ORIGINAL_SAVE_HOTKEY .*"map":"DividedAttention"',
        'ORIGINAL_LOAD_HOTKEY .*"map":"DividedAttention"', 'ORIGINAL_CAMPAIGN_DONE .*"map":"DividedAttention"')) {
        if ($logText -notmatch $marker) { $acceptance += "Missing $marker" }
    }
    if ($Map -in @('campaignfull','campaignall') -and $logText -notmatch 'ORIGINAL_MEDIUM_CAMPAIGN_DONE .*"won":true') {
        $acceptance += 'Original second map did not naturally win and show its settlement'
    }
    if ($Map -eq 'campaignall') {
        foreach ($marker in @('ORIGINAL_LATER_CAMPAIGN_WON .*"map":"MuttonsFolly".*"won":true',
            'ORIGINAL_LATER_CAMPAIGN_WON .*"map":"Madness".*"won":true',
            'ORIGINAL_ALL_CAMPAIGN_DONE .*"unlocked_endofgame":1')) {
            if ($logText -notmatch $marker) { $acceptance += "Missing $marker" }
        }
    }
}
$result = [pscustomobject]@{ mode=$Mode; map=$Map; operation=$operation; nativeExitCode=$process.ExitCode; acceptancePassed=($acceptance.Count -eq 0); acceptanceErrors=$acceptance; executable=$executable; executableSha256=$executableHash; sourceSha256=$(if ($buildManifest) { $buildManifest.sourceSha256 } else { $null }); buildBound=($null -ne $buildManifest); runnerSha256=$runnerHash; runDirectory=$runDirectory; log=$log; stdout=$stdout; stderr=$stderr }
# Preserve guest renderer warnings separately from bounded functional acceptance.
$diagnostics = $logText + "`n" + (Get-Content -LiteralPath $stdout -Raw) + "`n" + (Get-Content -LiteralPath $stderr -Raw)
$result | Add-Member -NotePropertyName glErrorCount -NotePropertyValue ([regex]::Matches($diagnostics, 'GL error after [^\r\n]+').Count)
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
