# Verify expensive original tower chains from one authentic earned-money checkpoint.
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$OutputDirectory = 'agent/codex/task05-offline-audit/verification',
    [ValidateSet('harvest','Fusion Cannon','Railgun VI','Tesla Coil','Flamethrower III')][string]$Case = 'harvest',
    [string]$Checkpoint = ''
)
$ErrorActionPreference = 'Stop'
# All process state and temporary files stay under the explicitly selected evidence root.
$project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$exe = [IO.Path]::GetFullPath($Executable)
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (!$Checkpoint) { $Checkpoint = Join-Path $root 'earned-checkpoint.bin' }
$Checkpoint = [IO.Path]::GetFullPath($Checkpoint)
$run = Join-Path $root (($Case -replace ' ','-') + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Force $run | Out-Null
$hash = (Get-FileHash -LiteralPath $exe).Hash
$headerHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'high-price-probe.hpp')).Hash
$manifestPath=Join-Path (Split-Path (Split-Path $exe -Parent) -Parent) 'build-manifest.json'
if (Test-Path -LiteralPath $manifestPath) {
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    if ($manifest.executableSha256 -ne $hash) { throw 'High-price build manifest does not match executable.' }
    foreach ($inputFile in $manifest.inputs) {
        if ((Get-FileHash -LiteralPath $inputFile.path).Hash -ne $inputFile.sha256) { throw "Frozen build input changed: $($inputFile.path)" }
    }
    Copy-Item -LiteralPath $manifestPath -Destination $run
}
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $run 'runner.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'high-price-probe.hpp') -Destination $run
$previousAudio=$env:TOWERMADNESS_X86_AUDIO
$previousCase=$env:TOWERMADNESS_HIGH_PRICE_CASE
$previousCheckpoint=$env:TOWERMADNESS_HIGH_PRICE_CHECKPOINT
try {
    $env:TOWERMADNESS_X86_AUDIO='0'
    $env:TOWERMADNESS_HIGH_PRICE_CASE=$Case
    $env:TOWERMADNESS_HIGH_PRICE_CHECKPOINT=$Checkpoint
    $package=Join-Path $project 'agent/codex/x86-reuse/package'
    $process=Start-Process -FilePath $exe -ArgumentList @(('"'+$package+'"'),'highprice:Peach',('"'+(Join-Path $run 'native.log')+'"')) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $run 'stdout.log') -RedirectStandardError (Join-Path $run 'stderr.log') -PassThru
} finally {
    $env:TOWERMADNESS_X86_AUDIO=$previousAudio
    $env:TOWERMADNESS_HIGH_PRICE_CASE=$previousCase
    $env:TOWERMADNESS_HIGH_PRICE_CHECKPOINT=$previousCheckpoint
}
$null=$process.Handle
$process.WaitForExit()
$log=Get-Content -LiteralPath (Join-Path $run 'native.log') -Raw
$diagnostics=$log+(Get-Content -LiteralPath (Join-Path $run 'stdout.log') -Raw)+(Get-Content -LiteralPath (Join-Path $run 'stderr.log') -Raw)
$glErrors=[regex]::Matches($diagnostics,'GL error after [^\r\n]+').Count
$failures=@()
if ($process.ExitCode -ne 0) { $failures += "Native exit $($process.ExitCode)" }
if ($glErrors -ne 0) { $failures += "OpenGL errors: $glErrors" }
if ($log -notmatch 'HIGH_PRICE_DONE ') { $failures += 'Missing completed acceptance' }
if ((Get-FileHash -LiteralPath $exe).Hash -ne $hash) { $failures += 'Executable changed' }
$resultPath=Join-Path $run 'high-price-result.json'
if (Test-Path -LiteralPath $resultPath) {
    $result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
    if (!$result.passed) { $failures += 'Native acceptance incomplete' }
    if ($result.case -ne $Case) { $failures += 'Native case mismatch' }
    if ($Case -ne 'harvest') {
        # These are the original Insane technology IDs observed from nativeTowerId, not substitute towers.
        $expectedTypes=@{'Fusion Cannon'=33;'Railgun VI'=42;'Tesla Coil'=38;'Flamethrower III'=2}
        if ($result.tower.type -ne $expectedTypes[$Case]) { $failures += 'Native target type mismatch' }
        if ($result.deliveredDamage -le 0) { $failures += 'Missing delivered damage' }
        foreach ($action in @('restore-exact','restored-attack','sold',('chain-complete-'+$Case))) {
            if ($action -notin $result.events.action) { $failures += "Missing original action: $action" }
        }
    }
} else { $failures += 'Missing native result' }
[ordered]@{case=$Case;executable=$exe;executableSha256=$hash;headerSha256=$headerHash;checkpoint=$Checkpoint;checkpointSha256=$(if(Test-Path -LiteralPath $Checkpoint){(Get-FileHash -LiteralPath $Checkpoint).Hash}else{$null});nativeExitCode=$process.ExitCode;glErrorCount=$glErrors;passed=($failures.Count-eq 0);failures=$failures;evidence=$run}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run 'acceptance.json')
Get-Content -LiteralPath (Join-Path $run 'acceptance.json')
if ($failures.Count) { throw ($failures -join '; ') }
