$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$archive = "$root/.deps/mpcaudio.7z"
if (-not (Test-Path $archive)) { Invoke-WebRequest 'https://github.com/Aleksoid1978/MPC-BE/releases/download/1.9.1/standalone_filters-mpc-be.1.9.1.x64.7z' -OutFile $archive -TimeoutSec 180 }
# Fixed digest published by the upstream release; use SHA-256 in the local binary manifest too.
if ((Get-FileHash $archive -Algorithm SHA1).Hash -ne 'F7EAB6A55B6491512F97C1B7560D81E6F2326075') { throw 'MPC-BE archive digest mismatch' }
New-Item -ItemType Directory -Force "$root/.deps/mpcaudio" | Out-Null
tar -xf $archive -C "$root/.deps/mpcaudio"
if ($LASTEXITCODE) { throw 'Archive extraction failed' }
Get-FileHash "$root/.deps/mpcaudio/standalone_filters-mpc-be.1.9.1.x64/MpcAudioRenderer.ax" -Algorithm SHA256
