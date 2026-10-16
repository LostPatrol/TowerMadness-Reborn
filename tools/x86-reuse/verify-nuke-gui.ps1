# Run the shared original-GUI verifier with the legal Mortar-to-H-Bomb click chain.
param(
    [string]$Executable = 'agent/codex/x86-reuse/current/Release/TowerMadnessX86Probe.exe',
    [string]$OutputDirectory = 'agent/codex/x86-reuse/nuke-gui',
    [string]$ExpectedExecutableSha256 = ''
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'verify-upgrade-gui.ps1') -Executable $Executable -OutputDirectory $OutputDirectory -ExpectedExecutableSha256 $ExpectedExecutableSha256 -ClickScript (Join-Path $PSScriptRoot 'nuke-clicks.json')
