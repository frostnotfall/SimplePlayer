param([int]$Seconds=24,[switch]$Animated)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$data=Join-Path $root $(if($Animated){'.local/subtitle-stress-data'}else{'.local/subtitle-scroll-data'})
New-Item -ItemType Directory -Force -Path $data | Out-Null
$resolver=if($Animated){'SubtitleStressResolver.as'}else{'SubtitleResolver.as'}
Copy-Item -LiteralPath "$root/tests/$resolver" -Destination "$root/.local/fixtures/$resolver"
@{schemaVersion=1;mediaScript="$root/.local/fixtures/$resolver";scriptEnabled=$true;statisticsEnabled=$false;chatVisible=$true;chatWidth=480;sidebarLayoutVersion=2;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
$playerArgs=@('--data-dir',$data,'--smoke-test','--smoke-seconds',"$Seconds",'--open','https://fixture.test/subtitles','--subtitle-scroll-exercise')
$quoted=$playerArgs | ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}
$process=Start-Process -FilePath "$root/dist/SimplePlayer/SimplePlayer.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardError "$data/gui-errors.log"
if(-not $process.WaitForExit(($Seconds+30)*1000)){throw 'Subtitle scroll check did not finish.'}
if($process.ExitCode){throw "Player exited with code $($process.ExitCode)"}
$result=Get-Content -LiteralPath "$data/smoke-result.json" -Raw | ConvertFrom-Json
$result.uiVerification | Format-Table check,passed
Get-Content -LiteralPath "$data/gui-errors.log"
if($result.error -or $result.uiVerification.Count -ne 33 -or ($result.uiVerification | Where-Object {-not $_.passed})){throw 'Subtitle visibility/fullscreen/scroll/seek regression failed.'}
Write-Output '33 subtitle visibility/fullscreen/paused repaint/scrollbar drag/wheel cadence/seek checks passed.'
