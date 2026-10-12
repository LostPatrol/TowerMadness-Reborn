# Fetch the approved pinned development dependencies into this project only.
param([string]$Proxy = $env:HTTPS_PROXY)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$dependencyRoot = Join-Path $projectRoot 'third_party'
New-Item -ItemType Directory -Force $dependencyRoot | Out-Null
# These digests bind the dependencies used for the first native baseline.
$downloads = @(
    @{ Name='SDL3-devel-3.4.18-VC.zip'; Url='https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip'; Hash='78D84602AE616CFE26B33A73B7D3B9B6B56E8A6650E53D92C9C6CA938F747ACD' },
    @{ Name='json.hpp'; Url='https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp'; Hash='AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63' },
    @{ Name='json-LICENSE.txt'; Url='https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT'; Hash=$null },
    @{ Name='stb_vorbis.c'; Url='https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_vorbis.c'; Hash='4C7CB2FF1F7011E9D67950446B7EB9CA044F2E464D76BFBB0B84DD2E23E65636' }
)
foreach ($download in $downloads) {
    $targetPath = Join-Path $dependencyRoot $download.Name
    $curlArguments = @('--fail','--location','--silent','--show-error','--max-time','60','--output',$targetPath)
    if ($Proxy) { $curlArguments += @('--proxy',$Proxy) }
    & curl.exe @curlArguments $download.Url
    if ($LASTEXITCODE -ne 0) { throw "Download failed: $($download.Name)" }
    if ($download.Hash -and (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash -ne $download.Hash) { throw "Dependency checksum mismatch: $($download.Name)" }
}
Expand-Archive -LiteralPath (Join-Path $dependencyRoot 'SDL3-devel-3.4.18-VC.zip') -DestinationPath $dependencyRoot -Force
Write-Output 'SDL3 3.4.18, nlohmann/json 3.12.0, and stb_vorbis 1.22 are ready in third_party.'
