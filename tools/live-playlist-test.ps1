param([string]$Ffmpeg='ffmpeg',[string]$Python='python',[switch]$PlaylistOnly,[string]$PlayerDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(-not $PlayerDirectory){$PlayerDirectory=Join-Path $root 'dist/SimplePlayer'}
$fixtures=Join-Path $root '.local/fixtures'
$data=Join-Path $root '.local/live-playlist-data'
New-Item -ItemType Directory -Force -Path $fixtures,$data | Out-Null
& $Ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=640x360:rate=25' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 12 -c:v libx264 -preset veryfast -g 50 -pix_fmt yuv420p -c:a aac "$fixtures/live-muxed.mp4"
if($LASTEXITCODE){throw 'Live fixture generation failed'}
Copy-Item -LiteralPath "$root/tests/LiveResolver.as" -Destination "$fixtures/LiveResolver.as"
Copy-Item -LiteralPath "$root/tests/PlaylistFocusResolver.as" -Destination "$fixtures/PlaylistFocusResolver.as"
function Run-Player([string]$Resolver,[string]$Url,[string]$Exercise,[int]$Seconds,[int]$Checks){
    @{schemaVersion=1;mediaScript="$fixtures/$Resolver";ffmpegPath=$Ffmpeg;scriptEnabled=$true;statisticsEnabled=$false;chatVisible=$true;chatWidth=480;sidebarLayoutVersion=2;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
    $playerArgs=@('--data-dir',$data,'--smoke-test','--smoke-seconds',"$Seconds",'--open',$Url,$Exercise,'--direct-network')
    $quoted=$playerArgs | ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}
    $player=Start-Process -FilePath "$PlayerDirectory/SimplePlayer.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardError "$data/gui-errors.log"
    if(-not $player.WaitForExit(($Seconds+30)*1000)){throw "Test player $($player.Id) did not exit"}
    if($player.ExitCode){throw "Test player failed: $($player.ExitCode)"}
    $result=Get-Content -LiteralPath "$data/smoke-result.json" -Raw | ConvertFrom-Json
    Copy-Item -LiteralPath "$data/smoke-result.json" -Destination "$data/$Resolver-result.json"
    $result.uiVerification | Format-Table check,passed,position,cacheStart,cacheEnd
    Get-Content -LiteralPath "$data/gui-errors.log"
    if($result.error -or $result.uiVerification.Count -ne $Checks -or ($result.uiVerification | Where-Object {-not $_.passed})){throw "Validation failed: $Exercise / $($result.error)"}
}
Run-Player 'PlaylistFocusResolver.as' 'https://fixture.test/item/40' '--playlist-focus-exercise' 24 39
if($PlaylistOnly){Write-Output '39 playlist/metadata/scroll preservation/new instance checks passed.';exit}
$serverArgs=@("$root/tests/live_server.py",$Ffmpeg,"$fixtures/live-muxed.mp4") | ForEach-Object {'"'+$_+'"'}
$server=Start-Process -FilePath $Python -ArgumentList $serverArgs -WindowStyle Hidden -PassThru -RedirectStandardError "$data/server-errors.log"
try { Run-Player 'LiveResolver.as' 'https://fixture.test/live' '--live-exercise' 40 14 }
finally { if(-not $server.HasExited){Stop-Process -Id $server.Id} }
Write-Output 'Playlist selection/scroll preservation/new instances and live rewind/pause/cache/catch-up/reopen passed.'
