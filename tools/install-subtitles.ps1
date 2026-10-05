$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$deps = "$root/.deps"
$commit = '1460b31b08c42cc2e9ac2c79f45ec8707e2675e2'
if (-not (Test-Path "$deps/vcpkg")) {
  Invoke-WebRequest "https://github.com/microsoft/vcpkg/archive/$commit.zip" -OutFile "$deps/vcpkg.zip" -TimeoutSec 180
  Expand-Archive -LiteralPath "$deps/vcpkg.zip" -DestinationPath "$deps/vcpkg-unpack" -Force
  Move-Item -LiteralPath "$deps/vcpkg-unpack/vcpkg-$commit" -Destination "$deps/vcpkg"
}
if (-not (Test-Path -LiteralPath "$deps/vcpkg/vcpkg.exe")) {
  & "$deps/vcpkg/bootstrap-vcpkg.bat" -disableMetrics
  if ($LASTEXITCODE) { throw 'vcpkg bootstrap failed' }
}
$env:VCPKG_DEFAULT_BINARY_CACHE = "$deps/vcpkg-cache"
New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
& "$deps/vcpkg/vcpkg.exe" install libass:x64-windows --vcpkg-root="$deps/vcpkg" --downloads-root="$deps/downloads" --x-install-root="$deps/vcpkg-installed" --disable-metrics
if ($LASTEXITCODE) { throw 'libass build failed' }
