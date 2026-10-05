param([string]$Ffmpeg='ffmpeg',[int]$Seconds=30)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$fixtures=Join-Path $root '.local/control-fixtures'
$data=Join-Path $root '.local/playback-controls-data'
New-Item -ItemType Directory -Force -Path $fixtures,$data | Out-Null
& $Ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=640x360:rate=30' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 60 -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a aac "$fixtures/muxed.mp4"
if($LASTEXITCODE){throw 'Control fixture generation failed'}
& $Ffmpeg -hide_banner -loglevel error -y -i "$fixtures/muxed.mp4" -an -c copy "$fixtures/video.mp4"
if($LASTEXITCODE){throw 'Video extraction failed'}
& $Ffmpeg -hide_banner -loglevel error -y -i "$fixtures/muxed.mp4" -vn -c copy "$fixtures/audio.m4a"
if($LASTEXITCODE){throw 'Audio extraction failed'}
Copy-Item -LiteralPath "$fixtures/audio.m4a" -Destination "$fixtures/audio-low.m4a" -Force
Copy-Item -LiteralPath "$root/tests/FixtureResolver.as" -Destination "$fixtures/FixtureResolver.as" -Force
@{schemaVersion=1;mediaScript="$fixtures/FixtureResolver.as";scriptEnabled=$true;statisticsEnabled=$false;chatVisible=$true;chatWidth=480;sidebarLayoutVersion=2;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
$playerArgs=@('--data-dir',$data,'--smoke-test','--smoke-seconds',"$Seconds",'--open','https://fixture.test/dual','--playback-controls-exercise','--screenshot',"$data/player.png")
$quoted=$playerArgs | ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}
$player=Start-Process -FilePath "$root/dist/SimplePlayer/SimplePlayer.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardError "$data/gui-errors.log"
if(-not $player.WaitForExit(($Seconds+30)*1000)){throw 'Control validation player did not exit'}
if($player.ExitCode){throw "Control validation player exited with $($player.ExitCode)"}
$result=Get-Content -LiteralPath "$data/smoke-result.json" -Raw | ConvertFrom-Json
$result.uiVerification | Format-Table check,passed,measured
Get-Content -LiteralPath "$data/gui-errors.log"
$required=@('pitchPreservingBackendAvailable','urlNativeKeyboardFocusReturns','urlEditableDuringNativePlayback','typingDoesNotChangePlaybackSpeed','urlRightClickMenuOpened','urlMenuCopiesSelection','urlMenuPastesClipboard','f6OpensPlaylist','f6ClosesPlaylist','f9OpensChat','f9ClosesChat','activeSidebarTabUsesRaisedColor','f9FullscreenOpensManualSidebar','f6FullscreenSwitchesToPlaylist','f6FullscreenClosesSidebar','speedMenuOpened','menuChangesActualRate','doubleSpeedMediaClock','halfSpeedMediaClock','invalidRatesRejected','zRestoresNormalRate','cIncreasesRate','xDecreasesRate','qmlWindowSpeedShortcut','pausedRateChangeDoesNotResume','pausedSeekRetainsRate','streamRebuildRetainsSpeed','normalSpeedAndVideoRestored','f9DisabledInLocalMode')
foreach($name in $required){$entry=$result.uiVerification | Where-Object check -eq $name;if(-not $entry -or -not $entry.passed){throw "Playback control check failed: $name"}}
if($result.uiVerification.Count -ne 36 -or ($result.uiVerification | Where-Object {-not $_.passed})){throw 'Not all 36 playback control checks passed'}
Write-Output '36 URL/native focus/clipboard/shortcut/speed/menu sizing/checkmarks/paused seek/stream rebuild/theme checks passed.'
