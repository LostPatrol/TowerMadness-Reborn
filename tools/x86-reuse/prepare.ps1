# Extract an immutable x86 APK into the isolated native-reuse experiment.
param([string]$Apk = 'E:/TowerMadness-Reborn/Madness_Zero-1.22.apk')
$ErrorActionPreference = 'Stop'
# Experiments never share the product's release directory or user save data.
$projectDirectory = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$experimentDirectory = Join-Path $projectDirectory 'agent/codex/x86-reuse'
$packageDirectory = Join-Path $experimentDirectory 'package'
New-Item -ItemType Directory -Force $packageDirectory | Out-Null
$packagePrefix = [IO.Path]::GetFullPath($packageDirectory) + [IO.Path]::DirectorySeparatorChar
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($Apk)
try {
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName -notmatch '^(lib/x86/|assets/)' -or $entry.FullName.EndsWith('/')) { continue }
        $destination = [IO.Path]::GetFullPath((Join-Path $packageDirectory $entry.FullName))
        if (!$destination.StartsWith($packagePrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'APK member escapes the experiment directory.' }
        New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $true)
    }
} finally { $archive.Dispose() }
Get-FileHash -LiteralPath $Apk -Algorithm SHA256 | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $experimentDirectory 'apk-identity.json') -Encoding utf8
& (Join-Path $PSScriptRoot 'prepare-gs.ps1')
Write-Output $packageDirectory
