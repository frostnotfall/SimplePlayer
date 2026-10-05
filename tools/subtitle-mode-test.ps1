param([int]$Seconds=20)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$fixtures=Join-Path $root '.local/fixtures'
$data=Join-Path $root '.local/subtitle-mode-data'
if(-not(Test-Path -LiteralPath "$fixtures/muxed.mp4")){throw 'Run tools/integration-test.ps1 to generate media fixtures first.'}
New-Item -ItemType Directory -Force -Path $data | Out-Null
Copy-Item -LiteralPath "$root/tests/SubtitleResolver.as" -Destination "$fixtures/SubtitleResolver.as"
@{schemaVersion=1;mediaScript="$fixtures/SubtitleResolver.as";scriptEnabled=$true;statisticsEnabled=$false;chatWidth=480;sidebarLayoutVersion=2;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
$playerArgs=@('--data-dir',$data,'--smoke-test','--smoke-seconds',"$Seconds",'--open','https://fixture.test/subtitles','--subtitle-exercise','--local-fixture',"$fixtures/muxed.mp4",'--screenshot',"$data/player.png")
$quoted=$playerArgs | ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}
$process=Start-Process -FilePath "$root/dist/SimplePlayer/SimplePlayer.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardError "$data/gui-errors.log"
if(-not $process.WaitForExit(($Seconds+30)*1000)){throw 'Validation player did not finish; inspect its pending media graph.'}
if($process.ExitCode){throw "Player exited with code $($process.ExitCode)"}
$result=Get-Content -LiteralPath "$data/smoke-result.json" -Raw | ConvertFrom-Json
$required=@('metadataDefaultPrimary','metadataDefaultDanmaku','metadataNameNotLanguage','inlineContentBothRendered','scriptDebugOutput','hidePrimaryRetainsSelection','showPrimaryRestoresContent','subtitleRadioSelection','subtitleTopMenuFourRows','subtitlePrimaryFitsText','subtitlePrimaryAdaptsFont','subtitleMenuAboveControls','subtitleMenuPositionStable','subtitleSubmenuContentWidth','subtitleMenusDarkTheme','subtitleMenusLiveLightTheme','subtitleMenusLiveDarkTheme','debugWindowKeepsVideo','windowHeightSetting','localModeSkipsAngelScript','localModeChatHidden','localPlaylistDeduplicated','stopKeepsLocalPlaylist','localPlaylistReopensWithoutScript','localPlaylistRemove')
$result.uiVerification | Format-Table check,passed
Get-Content -LiteralPath "$data/gui-errors.log"
$required += 'localDurationInformationUpdates'
foreach($name in $required){$entry=$result.uiVerification | Where-Object check -eq $name;if(-not $entry -or -not $entry.passed){throw "Feature did not pass: $name"}}
Write-Output 'Subtitle metadata/defaults/show-hide/menus/debug window/window height and script-free local playlist passed.'
