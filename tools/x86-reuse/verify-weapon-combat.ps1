# Run isolated original-weapon trigger checks and retain each bound run result.
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string[]]$Cases = @('flak','nuke','hbomb','flame','mine','bait','slow','boost','rail','missile','laser','plasma','mortar','shock')
)
$ErrorActionPreference = 'Stop'
# Each case's evidence parent owns its separate original-game state directory.
$results = @()
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
# Preserve completed cases when resuming a subset after a failed case.
$summaryPath = Join-Path $OutputDirectory 'weapon-combat-summary.json'
if (Test-Path -LiteralPath $summaryPath) {
    $results = @(Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json)
}
foreach ($case in $Cases) {
    $evidence = Join-Path $OutputDirectory "$case/evidence"
    & (Join-Path $PSScriptRoot 'run.ps1') -BuildDirectory $BuildDirectory -Mode weaponcombat -Map Peach -CombatCase $case -OutputDirectory $evidence -Silent
    $runFile = Get-ChildItem -LiteralPath $evidence -File -Filter '*-run.json' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $run = Get-Content -LiteralPath $runFile.FullName -Raw | ConvertFrom-Json
    $observed = Get-Content -LiteralPath (Join-Path $run.runDirectory 'weapon-combat-result.json') -Raw | ConvertFrom-Json
    $marker = Select-String -LiteralPath $run.log -Pattern '^WEAPON_COMBAT_PASSED ' | Select-Object -ExpandProperty Line
    $results = @($results | Where-Object { $_.case -ne $case })
    $results += [pscustomobject]@{ case=$case; marker=$marker; run=$run; config=$observed.config; counters=$observed.counters; final=$observed.final }
    $results | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8
    Write-Output "WEAPON_CASE_COMPLETE $case passed=$($run.acceptancePassed)"
}
