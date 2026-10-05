param([switch]$Package, [switch]$CoreOnly, [switch]$Fresh)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Install Visual Studio C++ desktop tools with the Windows SDK.' }
$dev = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
New-Item -ItemType Directory -Force -Path (Join-Path $root '.tools') | Out-Null
$envBatch = Join-Path $root '.tools/environment.cmd'
[IO.File]::WriteAllLines($envBatch, @('@echo off', "call `"$dev`" -arch=x64 -host_arch=x64 -vcvars_ver=14.44 >nul", 'set'))
$vars = & $env:ComSpec /d /c $envBatch
$compilerPath = ''
foreach ($line in $vars) {
  if ($line -match '^PATH=(.*)$') { $candidatePath = $matches[1]; if ($candidatePath -match '\\MSVC\\') { $compilerPath = $candidatePath }; continue }
  if ($line -match '^([^=]+)=(.*)$' -and $matches[1] -ine 'Path') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$env:Path = "$ninja;$root/.deps/Qt/6.8.3/msvc2022_64/bin;$root/.deps/vcpkg-installed/x64-windows/bin;$compilerPath"
$env:VSLANG = '1033'
Push-Location $root
try {
  $gui = if ($CoreOnly) { 'OFF' } else { 'ON' }
  $compiler = Join-Path $env:VCToolsInstallDir 'bin/Hostx64/x64/cl.exe'
  $configure = @('--preset', 'windows-x64', "-DBUILD_PLAYER=$gui", "-DCMAKE_C_COMPILER=$compiler", "-DCMAKE_CXX_COMPILER=$compiler", "-DCMAKE_MAKE_PROGRAM=$ninja/ninja.exe")
  if ($Fresh) { $configure += '--fresh' }
  & $cmake @configure
  if ($LASTEXITCODE) { throw 'Configure failed' }
  $buildArgs = @('--build','--preset','windows-x64','--parallel','8')
  if ($Fresh) { $buildArgs += '--clean-first' }
  & $cmake @buildArgs
  if ($LASTEXITCODE) { throw 'Build failed' }
  & (Join-Path (Split-Path $cmake) 'ctest.exe') --preset windows-x64
  if ($LASTEXITCODE) {
    foreach ($resultFile in @('build/contracts-results.txt','build/subtitle-results.txt')) {
      if (Test-Path -LiteralPath $resultFile) { Get-Content -LiteralPath $resultFile }
    }
    throw 'Tests failed'
  }
  if ($Package) {
    & $cmake --install build
    if ($LASTEXITCODE) { throw 'Install failed' }
    & "$root/.deps/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe" --release --qmldir "$root/player/qml" "$root/dist/SimplePlayer/SimplePlayer.exe"
    if ($LASTEXITCODE) { throw 'Qt deployment failed' }
  }
} finally { Pop-Location }
