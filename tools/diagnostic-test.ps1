param([int]$Seconds=12)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $root '.local/control-fixtures/muxed.mp4'
if(-not(Test-Path -LiteralPath $fixture)){throw 'Run tools/playback-controls-test.ps1 to generate the 60-second media fixture first.'}
$data=Join-Path $root ('.local/diagnostic-data-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $data | Out-Null
@{schemaVersion=1;scriptEnabled=$false;statisticsEnabled=$false;chatVisible=$true;chatWidth=480;sidebarLayoutVersion=2;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
$playerArgs=@('--data-dir',$data,'--smoke-test','--smoke-seconds',"$Seconds",'--open',$fixture,'--diagnostic-exercise')
$quoted=$playerArgs | ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}
$player=Start-Process -FilePath "$root/dist/SimplePlayer/SimplePlayer.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardError "$data/gui-errors.log"
if(-not $player.WaitForExit(($Seconds+30)*1000)){throw "Diagnostic test player $($player.Id) did not exit"}
if($player.ExitCode){throw "Diagnostic test player failed: $($player.ExitCode)"}
$result=Get-Content -LiteralPath "$data/smoke-result.json" -Raw | ConvertFrom-Json
$result.uiVerification | Format-Table check,passed
Get-Content -LiteralPath "$data/gui-errors.log"
Write-Output "Diagnostic artifacts: $data"
if($result.error -or $result.uiVerification.Count -ne 21 -or ($result.uiVerification | Where-Object {-not $_.passed})){throw "Diagnostic window validation failed: $($result.error)"}
Write-Output '21 diagnostic window/normal/fullscreen/resize/theme/copy/refresh/export/error-feedback checks passed.'
