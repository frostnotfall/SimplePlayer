param([string]$AngelScriptSource = '', [string]$ScriptRepository = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $root '.deps'
New-Item -ItemType Directory -Force -Path $deps | Out-Null
function FetchZip([string]$name, [string]$url, [string]$folder) {
  if (Test-Path "$deps/$name") { return }
  $archive = "$deps/$name.zip"
  Invoke-WebRequest $url -OutFile $archive -TimeoutSec 180
  Expand-Archive -LiteralPath $archive -DestinationPath "$deps/unpack-$name" -Force
  Move-Item -LiteralPath "$deps/unpack-$name/$folder" -Destination "$deps/$name"
  Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Select-Object Path,Hash
}
if (-not (Test-Path "$deps/angelscript")) {
  if ($AngelScriptSource) { Copy-Item -LiteralPath $AngelScriptSource -Destination "$deps/angelscript" -Recurse }
  else { FetchZip 'angelscript' 'https://www.angelcode.com/angelscript/sdk/files/angelscript_2.38.0.zip' 'sdk' }
}
FetchZip 'jsoncpp' 'https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.9.6.zip' 'jsoncpp-1.9.6'
FetchZip 'zlib' 'https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip' 'zlib-1.3.1'
FetchZip 'mpcvr-source' 'https://github.com/Aleksoid1978/VideoRenderer/archive/refs/tags/0.9.7.zip' 'VideoRenderer-0.9.7'
if (-not (Test-Path "$deps/lav-source/LAVFilters-0.80/common/baseclasses")) {
  Invoke-WebRequest 'https://github.com/Nevcairiel/LAVFilters/archive/refs/tags/0.80.zip' -OutFile "$deps/lav-source.zip" -TimeoutSec 180
  Expand-Archive -LiteralPath "$deps/lav-source.zip" -DestinationPath "$deps/lav-source" -Force
}
# Two source-only header corrections for modern MSVC; no interface or layout changes.
foreach ($entry in @(@('transip.h','CTransInPlaceFilter::Copy','Copy'),@('videoctl.h','CAggDirectDraw::~CAggDirectDraw','~CAggDirectDraw'))) {
  $path = "$deps/lav-source/LAVFilters-0.80/common/baseclasses/$($entry[0])"
  $source = [IO.File]::ReadAllText($path)
  [IO.File]::WriteAllText($path, $source.Replace($entry[1],$entry[2]), [Text.UTF8Encoding]::new($false))
}
if (-not (Test-Path "$deps/webview2")) {
  $archive = "$deps/webview2.zip"
  Invoke-WebRequest 'https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.3124.44/microsoft.web.webview2.1.0.3124.44.nupkg' -OutFile $archive -TimeoutSec 180
  Expand-Archive -LiteralPath $archive -DestinationPath "$deps/webview2" -Force
  Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Select-Object Path,Hash
}
New-Item -ItemType Directory -Force -Path "$deps/interfaces" | Out-Null
$interfaces = @{
  'SubRenderIntf.h' = 'https://raw.githubusercontent.com/Aleksoid1978/VideoRenderer/0.9.7/Include/SubRenderIntf.h'
}
foreach ($file in $interfaces.Keys) {
  if (-not (Test-Path "$deps/interfaces/$file")) {
    Invoke-WebRequest $interfaces[$file] -OutFile "$deps/interfaces/$file" -TimeoutSec 60
  }
}
if ($ScriptRepository) {
  $commit = & git -C $ScriptRepository rev-parse HEAD
  if ($commit -ne '07a06f272278f602e65af3fa3acba7d217eecb56') { throw "Unexpected script baseline: $commit" }
  $local = "$root/.local/scripts"
  New-Item -ItemType Directory -Force -Path $local | Out-Null
  & git -C $ScriptRepository archive -o "$root/.local/scripts.zip" $commit Media/PlayParse Playback/Statistics
  if ($LASTEXITCODE) { throw 'Script archive failed' }
  Expand-Archive -LiteralPath "$root/.local/scripts.zip" -DestinationPath $local -Force
  # Extract committed defaults; do not copy working-tree account configuration.
  Get-FileHash -LiteralPath "$local/Media/PlayParse/MediaPlayParse - Bilibili.as","$local/Playback/Statistics/PlaybackStatistics - Bilibili.as" | Select-Object Path,Hash
}
Write-Output 'Source dependencies ready. Install Qt 6.8.3 msvc2022_64 under .deps/Qt, then run tools/build.ps1.'
