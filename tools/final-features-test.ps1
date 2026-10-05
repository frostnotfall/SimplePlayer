param([string]$Ffmpeg='ffmpeg',[int]$Seconds=26)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$data=Join-Path $root '.local/final-features-data'
New-Item -ItemType Directory -Force -Path $data | Out-Null
$video=Join-Path $data 'repeat.mp4'
& $Ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=640x360:rate=24' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 2 -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac $video
if($LASTEXITCODE){throw 'Repeat fixture generation failed.'}
"1`n00:00:00,000 --> 00:02:00,000`nPrimary loop caption`n`n" | Set-Content -LiteralPath "$data/repeat-primary.srt" -Encoding utf8
"1`n00:00:00,000 --> 00:02:00,000`nSecondary loop caption`n`n" | Set-Content -LiteralPath "$data/repeat-secondary.srt" -Encoding utf8
@{schemaVersion=1;scriptEnabled=$false;statisticsEnabled=$false;chatVisible=$true;chatWidth=480;sidebarLayoutVersion=2;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
$playerArgs=@('--data-dir',$data,'--smoke-test','--smoke-seconds',"$Seconds",'--open',$video,'--final-features-exercise')
$quoted=$playerArgs | ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}
$process=Start-Process -FilePath "$root/dist/SimplePlayer/SimplePlayer.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardError "$data/gui-errors.log"
if(-not $process.WaitForExit(($Seconds+20)*1000)){Stop-Process -Id $process.Id;throw 'Final features check timed out.'}
if($process.ExitCode){throw "Player exited with code $($process.ExitCode)"}
$result=Get-Content -LiteralPath "$data/smoke-result.json" -Raw | ConvertFrom-Json
$result.uiVerification | Format-Table check,passed
Get-Content -LiteralPath "$data/gui-errors.log"
if($result.error -or $result.uiVerification.Count -ne 34 -or ($result.uiVerification | Where-Object {-not $_.passed})){throw "Repeat/fullscreen/sidebar regression failed: $($result.error)"}
Write-Output '34 repeat/seek/fullscreen/manual-sidebar/animation checks passed.'
