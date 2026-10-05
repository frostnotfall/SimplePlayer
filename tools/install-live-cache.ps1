param([Parameter(Mandatory=$true)][string]$Ffmpeg)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$source=(Resolve-Path -LiteralPath $Ffmpeg).Path
$target=Join-Path $root '.deps/live-cache'
New-Item -ItemType Directory -Force -Path $target | Out-Null
$license=& $source -L 2>&1
if($LASTEXITCODE){throw 'Cannot read FFmpeg license/version'}
Copy-Item -LiteralPath $source -Destination "$target/ffmpeg.exe"
$license | Set-Content -LiteralPath "$target/FFMPEG-LICENSE.txt" -Encoding utf8
@{component='FFmpeg CLI (remux only)';sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;version=(@($license | Where-Object {"$_" -match '^ffmpeg version'}) | Select-Object -First 1).ToString();buildInfo='FFMPEG-LICENSE.txt';upstream='https://ffmpeg.org/';sourceDistributor='https://www.gyan.dev/ffmpeg/builds/';developmentPackage=$true} | ConvertTo-Json | Set-Content -LiteralPath "$target/manifest.json" -Encoding utf8
