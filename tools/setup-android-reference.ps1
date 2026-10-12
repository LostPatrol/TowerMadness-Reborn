# Downloads an isolated Android reference runtime without changing Windows features.
# Use -LaunchOriginal to boot the AVD and install/launch the unchanged baseline APK.
[CmdletBinding()]
param(
    [string]$RuntimeDirectory = '',
    [string]$Proxy = 'http://127.0.0.1:7897',
    [ValidateSet('default', 'google_apis', 'google_apis_playstore')]
    [string]$ImageTag = 'google_apis',
    [ValidateSet('arm', 'x86')]
    [string]$Architecture = 'arm',
    [switch]$PlayStore,
    [switch]$LaunchOriginal
)

$ErrorActionPreference = 'Stop'
if ($ImageTag -eq 'google_apis_playstore') { $PlayStore = $true }

# Resolve paths relative to this project, never the caller's working directory.
$projectDirectory = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeDirectory) {
    $RuntimeDirectory = Join-Path $projectDirectory 'agent/codex/android-reference/runtime'
}
$RuntimeDirectory = [System.IO.Path]::GetFullPath($RuntimeDirectory)
# Keep downloads, SDK, device data and logs under one explicit runtime directory.
$downloadsDirectory = Join-Path $RuntimeDirectory 'downloads'
$sdkDirectory = Join-Path $RuntimeDirectory 'sdk'
$avdRoot = Join-Path $RuntimeDirectory 'avd'
# Use a separate port and AVD so an existing Android session is not selected accidentally.
$avdName = 'TM_Original_API17_ARM'
if ($ImageTag -eq 'google_apis') { $avdName = 'TM_Original_API17_Google_ARM' }
$serial = 'emulator-5580'
if ($Architecture -eq 'x86') { $avdName = 'TM_Original_API17_Google_x86'; $serial = 'emulator-5582' }
# A real Google Play image supplies the startup service missing from old Google APIs images.
$apiLevel = 17
if ($PlayStore) {
    $Architecture = 'x86'
    $ImageTag = 'google_apis_playstore'
    $apiLevel = 24
    $avdName = 'TM_Original_API24_Play_x86'
    $serial = 'emulator-5584'
}
$avdDirectory = Join-Path $avdRoot "$avdName.avd"
$logsDirectory = Join-Path (Join-Path $RuntimeDirectory 'logs') $avdName
# Google Play background services need more RAM than the legacy bare image.
$memoryMegabytes = 512
if ($PlayStore) { $memoryMegabytes = 1536 }
# Each original APK variant must use the matching native ABI.
$imageAbi = 'armeabi-v7a'
$cpuModel = 'cortex-a8'
if ($Architecture -eq 'x86') { $imageAbi = 'x86'; $cpuModel = 'qemu32' }
if ($Architecture -eq 'x86' -and -not $PlayStore) { $ImageTag = 'google_apis' }
$systemImageDirectory = Join-Path $sdkDirectory "system-images/android-$apiLevel/$ImageTag/$imageAbi"
# The original APK identity is shared with MEMORY.md and must remain unchanged.
$apkPath = Join-Path $projectDirectory 'TowerMadness+Zero_+3D+TD_1.22_APKPure.apk'
$apkSha256 = '0bc4ec90ebf0eab249cab4c39b9390c1f05dafffd83f92adcf09251890b3cb8b'
if ($Architecture -eq 'x86') {
    # The user selected this x86 1.22 candidate separately from the ARM reconstruction baseline.
    $apkPath = Join-Path $projectDirectory 'Madness_Zero-1.22.apk'
    $apkSha256 = '7e8d09079c963e36f4ba633712b112f65d410740405e828405404fd729afd3dd'
}

# These sizes and SHA1 values come from Google's repository XML, not third-party mirrors.
$packages = @(
    @{
        Name = 'tools_r25.2.5-windows.zip'
        Url = 'https://dl.google.com/android/repository/tools_r25.2.5-windows.zip'
        Size = 306785944L
        Sha1 = 'a7f7ebeae1c8d8f62d3a8466e9c81baee7cc31ca'
        Destination = $sdkDirectory
        Marker = Join-Path $sdkDirectory 'tools/emulator-arm.exe'
    },
    @{
        Name = 'armeabi-v7a-17_r06.zip'
        Url = 'https://dl.google.com/android/repository/sys-img/android/armeabi-v7a-17_r06.zip'
        Size = 124437041L
        Sha1 = 'a18a3fd0958ec4ef52507f58e414fc5c7dfd59d6'
        Destination = Split-Path -Parent $systemImageDirectory
        Marker = Join-Path $systemImageDirectory 'system.img'
    },
    @{
        Name = 'platform-tools_r37.0.1-win.zip'
        Url = 'https://dl.google.com/android/repository/platform-tools_r37.0.1-win.zip'
        Size = 8044989L
        Sha1 = 'e03e78b1d80b396f1c3358e31251cb31740e1110'
        Destination = $sdkDirectory
        Marker = Join-Path $sdkDirectory 'platform-tools/adb.exe'
    }
)
if ($ImageTag -eq 'google_apis') {
    # Keep the alternative Google image and its archive separate from the default-image evidence.
    $packages[1].Name = 'google-apis-armeabi-v7a-17_r06.zip'
    $packages[1].Url = 'https://dl.google.com/android/repository/sys-img/google_apis/armeabi-v7a-17_r06.zip'
    $packages[1].Size = 174631794L
    $packages[1].Sha1 = 'a59f26cb5707da97e869a27d87b83477204ac594'
}
if ($Architecture -eq 'x86') {
    $packages[1].Name = 'google-apis-x86-17_r07.zip'
    $packages[1].Url = 'https://dl.google.com/android/repository/sys-img/google_apis/x86-17_r07.zip'
    $packages[1].Size = 194811128L
    $packages[1].Sha1 = '1ad5ffb51e31f5fe9fa47411fed2c2ade9a33865'
}
if ($PlayStore) {
    $packages[0].Name = 'emulator-windows_x64-16428233.zip'
    $packages[0].Url = 'https://dl.google.com/android/repository/emulator-windows_x64-16428233.zip'
    $packages[0].Size = 455342868L
    $packages[0].Sha1 = '488ed747e82de7e9bb5247becd1ac043c7e5e85d'
    $packages[0].Marker = Join-Path $sdkDirectory 'emulator/emulator.exe'
    $packages[1].Name = 'google-play-x86-24_r19.zip'
    $packages[1].Url = 'https://dl.google.com/android/repository/sys-img/google_apis_playstore/x86-24_r19.zip'
    $packages[1].Size = 812724041L
    $packages[1].Sha1 = 'b52e9593ffdde65c1a0970256a32e8967c89cc22'
}

# Use Git's curl when available because this computer's system curl has had TLS failures.
$curlExecutable = 'D:/Git/mingw64/bin/curl.exe'
if (-not (Test-Path -LiteralPath $curlExecutable)) {
    $curlExecutable = (Get-Command curl.exe -ErrorAction Stop).Source
}

# Fetch and validate one pinned archive, preserving a completed download for repeat runs.
function Get-ReferenceArchive {
    param([hashtable]$Package)
    $archivePath = Join-Path $downloadsDirectory $Package.Name
    $validArchive = (Test-Path -LiteralPath $archivePath) -and
        ((Get-Item -LiteralPath $archivePath).Length -eq $Package.Size) -and
        ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA1).Hash -eq $Package.Sha1)
    if (-not $validArchive) {
        $partialPath = "$archivePath.download"
        $curlArguments = @('--fail', '--location', '--retry', '2', '--connect-timeout', '30')
        if ($Proxy) { $curlArguments += @('--proxy', $Proxy) }
        $curlArguments += @('--output', $partialPath, $Package.Url)
        & $curlExecutable @curlArguments
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($Package.Name)" }
        if ((Get-Item -LiteralPath $partialPath).Length -ne $Package.Size -or
            (Get-FileHash -LiteralPath $partialPath -Algorithm SHA1).Hash -ne $Package.Sha1) {
            throw "Archive does not match Google's repository: $partialPath"
        }
        Move-Item -LiteralPath $partialPath -Destination $archivePath -Force
    }
    return $archivePath
}

# Execute adb only against this reference emulator and surface any command failure.
function Invoke-ReferenceAdb {
    param([string[]]$Arguments)
    & $adbExecutable -s $serial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Reference adb failed: $($Arguments -join ' ')" }
}

Write-Host "Reference runtime: $RuntimeDirectory"
$downloadBytes = ($packages | Measure-Object -Property Size -Sum).Sum
Write-Host "Pinned downloads: $downloadBytes bytes, plus extraction and AVD storage."
Write-Host 'No Windows feature, system PATH, or original APK will be changed.'
foreach ($directory in @($downloadsDirectory, $sdkDirectory, $avdRoot, $avdDirectory, $logsDirectory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($package in $packages) {
    $archivePath = Get-ReferenceArchive -Package $package
    if (-not (Test-Path -LiteralPath $package.Marker)) {
        New-Item -ItemType Directory -Path $package.Destination -Force | Out-Null
        [System.IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $package.Destination)
    }
}

# Manual AVD configuration avoids the obsolete Java manager and its JDK8 dependency.
$avdIniPath = Join-Path $avdRoot "$avdName.ini"
if (-not (Test-Path -LiteralPath $avdIniPath)) {
    @(
        'avd.ini.encoding=UTF-8',
        "path=$avdDirectory",
        "target=android-$apiLevel"
    ) | Set-Content -LiteralPath $avdIniPath -Encoding utf8
}
$configPath = Join-Path $avdDirectory 'config.ini'
if (-not (Test-Path -LiteralPath $configPath)) {
    @(
        'avd.ini.encoding=UTF-8',
        "AvdId=$avdName",
        "avd.ini.displayname=$avdName",
        "abi.type=$imageAbi",
        "hw.cpu.arch=$Architecture",
        "hw.cpu.model=$cpuModel",
        "hw.ramSize=$memoryMegabytes",
        'hw.cpu.ncore=1',
        'vm.heapSize=128',
        'hw.lcd.width=480',
        'hw.lcd.height=800',
        'hw.lcd.density=240',
        'hw.lcd.depth=32',
        'hw.gpu.enabled=yes',
        'hw.gpu.mode=host',
        'hw.keyboard=yes',
        'hw.accelerometer=yes',
        'hw.audioInput=no',
        'hw.audioOutput=no',
        'hw.sdCard=yes',
        'skin.name=480x800',
        'skin.path=480x800',
        "tag.id=$ImageTag",
        # The legacy launcher always joins this setting to the SDK root, even for absolute paths.
        "image.sysdir.1=system-images/android-$apiLevel/$ImageTag/$imageAbi/",
        'disk.dataPartition.size=512M',
        'sdcard.size=512M',
        "sdcard.path=$(Join-Path $avdDirectory 'sdcard.img')"
    ) | Set-Content -LiteralPath $configPath -Encoding utf8
}
$sdcardPath = Join-Path $avdDirectory 'sdcard.img'
if (-not (Test-Path -LiteralPath $sdcardPath)) {
    & (Join-Path $sdkDirectory 'tools/mksdcard.exe') '512M' $sdcardPath
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the reference SD card image.' }
}

# Record downloaded content identities for later comparison without changing the APK.
$packages | ForEach-Object {
    $archivePath = Join-Path $downloadsDirectory $_.Name
    [ordered]@{
        name = $_.Name
        url = $_.Url
        sha1 = $_.Sha1
        sha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $RuntimeDirectory "download-identities-$ImageTag-$Architecture.json") -Encoding utf8

$emulatorExecutable = Join-Path $sdkDirectory 'tools/emulator.exe'
if ($PlayStore) { $emulatorExecutable = Join-Path $sdkDirectory 'emulator/emulator.exe' }
$adbExecutable = Join-Path $sdkDirectory 'platform-tools/adb.exe'
Write-Host "Reference emulator: $emulatorExecutable"
Write-Host "Reference adb: $adbExecutable"
if (-not $LaunchOriginal) {
    Write-Host 'Setup complete. Use -LaunchOriginal to boot and capture the original game.'
    return
}
if (-not $PlayStore) {
    Write-Warning 'The tested API17 images lack the startup service needed by this unchanged game; menu entry remains unverified.'
}
if ((Get-FileHash -LiteralPath $apkPath -Algorithm SHA256).Hash -ne $apkSha256) {
    throw "The original APK differs from its selected reference identity: $apkPath"
}

# Scope SDK environment variables to this script and its emulator child process.
$previousSdkRoot = $env:ANDROID_SDK_ROOT
$previousAndroidHome = $env:ANDROID_HOME
$previousAvdHome = $env:ANDROID_AVD_HOME
try {
    $env:ANDROID_SDK_ROOT = $sdkDirectory
    $env:ANDROID_HOME = $sdkDirectory
    $env:ANDROID_AVD_HOME = $avdRoot
    # An offline serial is still a booting AVD; launching it again would hit its device-data lock.
    $existingDevice = @(& $adbExecutable devices) | Where-Object { $_ -match "^$serial\s" }
    $emulatorProcess = $null
    if (-not $existingDevice) {
        $launchArguments = @(
            '-avd', $avdName, '-port', ($serial -replace 'emulator-', ''),
            '-accel', 'off', '-no-audio', '-no-snapshot'
        )
        if ($PlayStore) {
            # Headless software GLES avoids the observed current Qt/host-renderer startup exit.
            $launchArguments += @('-no-window', '-no-metrics', '-gpu', 'software', '-feature', '-Vulkan')
        } else {
            # Explicit classic selection avoids this SDK launcher's incompatible ARM QEMU2 fallback.
            $launchArguments += @('-engine', 'classic', '-gpu', 'host')
        }
        # Hidden process launch avoids an extra console; the legacy emulator has its own UI.
        $emulatorProcess = Start-Process -FilePath $emulatorExecutable -WindowStyle Hidden -PassThru `
          -ArgumentList $launchArguments -RedirectStandardOutput (Join-Path $logsDirectory 'emulator.stdout.log') `
          -RedirectStandardError (Join-Path $logsDirectory 'emulator.stderr.log')
    }
    # Slow ARM emulation can require several minutes; do not impose a short boot deadline.
    $bootDeadline = [DateTime]::UtcNow.AddMinutes(10)
    $booted = $false
    while ([DateTime]::UtcNow -lt $bootDeadline) {
        if ($emulatorProcess -and $emulatorProcess.HasExited) {
            throw "Reference emulator exited with code $($emulatorProcess.ExitCode). Inspect $logsDirectory"
        }
        $bootStatus = & $adbExecutable -s $serial shell getprop sys.boot_completed 2>$null
        if ($bootStatus -eq '1') { $booted = $true; break }
        Start-Sleep -Seconds 5
    }
    if (-not $booted) { throw "AVD boot timed out. Inspect $logsDirectory" }
    Invoke-ReferenceAdb -Arguments @('shell', 'getprop') |
        Set-Content -LiteralPath (Join-Path $logsDirectory 'android-properties.txt') -Encoding utf8
    Invoke-ReferenceAdb -Arguments @('install', '-r', $apkPath)
    Invoke-ReferenceAdb -Arguments @('shell', 'am', 'start', '-n',
        'com.limbicsoftware.towermadnesszero/com.apportable.activity.VerdeActivity')
    Write-Host 'Activity start requested; verify the menu and PlainPlane visually before claiming compatibility.'
} finally {
    $env:ANDROID_SDK_ROOT = $previousSdkRoot
    $env:ANDROID_HOME = $previousAndroidHome
    $env:ANDROID_AVD_HOME = $previousAvdHome
}
