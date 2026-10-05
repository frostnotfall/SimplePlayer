param([int]$Seconds=16)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$data=Join-Path $root '.local/window-layout-data'
$media=Join-Path $root '.local/fixtures/muxed.mp4'
if(-not(Test-Path -LiteralPath $media)){throw 'Generate media fixtures with tools/integration-test.ps1 first.'}
New-Item -ItemType Directory -Force -Path $data | Out-Null
@{schemaVersion=1;scriptEnabled=$false;chatVisible=$true;chatWidth=480;sidebarLayoutVersion=2;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
function Run-WindowCheck([string[]]$Extra,[int]$Duration){
    $playerArgs=@('--data-dir',$data,'--smoke-test','--smoke-seconds',"$Duration")+$Extra
    $quoted=$playerArgs | ForEach-Object {'"'+($_ -replace '(\\*)"','$1$1\"' -replace '(\\+)$','$1$1')+'"'}
    $process=Start-Process -FilePath "$root/dist/SimplePlayer/SimplePlayer.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru -RedirectStandardError "$data/gui-errors.log"
    if(-not $process.WaitForExit(($Duration+30)*1000)){throw 'Window check did not finish.'}
    if($process.ExitCode){throw "Player exited with code $($process.ExitCode)"}
    Get-Content -LiteralPath "$data/gui-errors.log" | Write-Host
    return Get-Content -LiteralPath "$data/smoke-result.json" -Raw | ConvertFrom-Json
}
$first=Run-WindowCheck -Extra @('--open',$media,'--window-exercise') -Duration $Seconds
Copy-Item -LiteralPath "$data/smoke-result.json" -Destination "$data/actions.json"
$required=@('settingsSizePersisted','windowButtonsUniform','windowButtonsDrawnIcons','sidebarCloseContractsWindow','sidebarClosePreservesVideoWidth','sidebarOpenExpandsWindow','sidebarOpenPreservesVideoWidth','sidebarRoundTripSizeSaved','maximizePreservesNormalSize','maximizeButtonRestoreIcon','unmaximizeRestoresNormalSize','fullscreenPreservesNormalSize','fullscreenExitRestoresSize','manualResizeSizePersisted','minimizeButtonPreservesSize','minimizeRestoreSize')
$first.uiVerification | Format-Table check,passed
foreach($name in $required){$entry=$first.uiVerification | Where-Object check -eq $name;if(-not $entry -or -not $entry.passed){throw "Window action did not pass: $name"}}
$resizeChecks=@('rightEdgeInputGeometry','rightEdgeOnlyResizesSidebar','rightEdgeSavesSidebarWidth','leftEdgeOnlyResizesVideo','dividerOnlyResizesVideo','closedRightEdgeResizesVideo','closedLeftEdgeResizesVideo','resizeRulesFinalGeometrySaved')
foreach($name in $resizeChecks){$entry=$first.uiVerification | Where-Object check -eq $name;if(-not $entry -or -not $entry.passed){throw "Resize rule did not pass: $name"}}
$second=Run-WindowCheck -Extra @() -Duration 2
if($second.windowWidth -ne 1280 -or $second.windowHeight -ne 740){throw 'Restart did not restore normal window dimensions.'}
Write-Output '24 window checks and restart size restoration passed.'
