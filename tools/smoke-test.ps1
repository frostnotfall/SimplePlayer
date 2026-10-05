param([string]$Open = '', [int]$Seconds = 6, [string]$DataDirectory = '', [switch]$Exercise, [switch]$UiExercise, [string]$PrimarySubtitle = '', [string]$SecondarySubtitle = '', [switch]$EmbeddedMain, [switch]$DirectNetwork)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$app = "$root/dist/SimplePlayer/SimplePlayer.exe"
if (-not (Test-Path $app)) { throw 'Build with tools/build.ps1 -Package first.' }
$qa = if ($DataDirectory) { $DataDirectory } else { "$root/.local/qa-data" }
New-Item -ItemType Directory -Force -Path $qa | Out-Null
$args = @('--data-dir', $qa, '--smoke-test', '--smoke-seconds', "$Seconds", '--screenshot', "$root/.local/player-smoke.png")
if ($Open) { $args += @('--open', $Open) }
if ($Exercise) { $args += '--exercise' }
if ($UiExercise) { $args += '--ui-exercise' }
if ($DirectNetwork) { $args += '--direct-network' }

if ($EmbeddedMain) { $args += '--embedded-main' }
if ($PrimarySubtitle) { $args += @('--subtitle-main', $PrimarySubtitle) }
if ($SecondarySubtitle) { $args += @('--subtitle-secondary', $SecondarySubtitle) }
$quotedArgs = $args | ForEach-Object { '"' + ($_ -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"' }
$process = Start-Process -FilePath $app -ArgumentList $quotedArgs -WindowStyle Hidden -PassThru -RedirectStandardError "$root/.local/gui-errors.log"
if (-not $process.WaitForExit(($Seconds + 45) * 1000)) { throw 'Player has not closed: inspect the pending graph; no thread was force-terminated.' }
if ($process.ExitCode) { throw "Player failed with exit code $($process.ExitCode)." }
Get-Content "$qa/smoke-result.json" -Raw
Get-Content "$root/.local/gui-errors.log"
if ($UiExercise) {
  $uiResult = Get-Content -LiteralPath "$qa/smoke-result.json" -Raw | ConvertFrom-Json
  foreach ($check in @('defaultSidebar480','nativeVideoVisible','videoDividerPreservesSidebar','windowResizePreservesSavedWidth','settingsKeepsNativeVideoAndSidebar','tracksKeepsNativeVideoAndSidebar','dialogsPreservePlayback','nativeResizeCursorRestores','mutePreservesVolume','volumeWheelAndPercent','volumeLowerBound','parsePlaylistOptionRemoved','playlistWheelZoom','playlistScrollbar','windowsPathSelectable','pathCopyPaste','resizableSettingsAndWindowWidth','comboPopupPositionStable','menuPopupPositionStable')) {
    $entry = $uiResult.uiVerification | Where-Object check -eq $check
    if (-not $entry -or -not $entry.passed) { throw "UI action did not pass: $check" }
  }
}
if ($Exercise) {
  $result = Get-Content -LiteralPath "$qa/smoke-result.json" -Raw | ConvertFrom-Json
  foreach ($event in @('opened','paused','seeked','resumed','switched','stopped','reopened')) {
    if ($result.verification -notcontains $event) { throw "Playback action did not pass: $event" }
  }
  if ($result.diagnostics.videoConnected) {
    $video = $result.diagnostics
    if ($video.videoDestinationWidth -le 0 -or $video.videoDestinationHeight -le 0 -or $video.rendererFramesDrawn -lt 1) { throw 'Renderer has no valid destination rectangle or drawn frames.' }
    if ($video.videoDestinationLeft -lt 0 -or $video.videoDestinationTop -lt 0 -or ($video.videoDestinationLeft + $video.videoDestinationWidth) -gt $video.videoHostWidth -or ($video.videoDestinationTop + $video.videoDestinationHeight) -gt $video.videoHostHeight) { throw 'Video destination extends beyond its host.' }
  }
}
