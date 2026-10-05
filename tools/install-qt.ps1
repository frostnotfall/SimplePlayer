param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$env:PYTHONPATH = "$root/.tools/aqt"
if (-not (Test-Path -LiteralPath "$root/.tools/aqt/aqt")) {
  & $Python -m pip install --target "$root/.tools/aqt" 'aqtinstall==3.3.0'
  if ($LASTEXITCODE) { throw 'aqtinstall installation failed' }
}
& $Python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir "$root/.deps/Qt" --archives qtbase qtdeclarative qtsvg qtshadertools
if ($LASTEXITCODE) { throw 'Qt download failed' }
