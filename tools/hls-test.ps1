param([string]$Ffmpeg = 'ffmpeg', [string]$Python = 'python', [int]$Seconds = 65)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fixtures = "$root/.local/hls-fixture"
$data = "$root/.local/hls-test-data"
New-Item -ItemType Directory -Force $fixtures,$data | Out-Null
if (-not (Test-Path "$root/.local/fixtures/muxed.mp4")) { throw 'Run integration-test.ps1 first to generate synthetic media.' }
& $Ffmpeg -hide_banner -loglevel error -y -stream_loop -1 -i "$root/.local/fixtures/muxed.mp4" -t ($Seconds+30) -c copy -hls_time 2 -hls_list_size 0 -hls_playlist_type vod "$fixtures/index.m3u8"
if ($LASTEXITCODE) { throw 'HLS generation failed' }
$resolver = @'
bool PlayitemCheck(const string &in url) {return url == "https://fixture.test/hls";}
string PlayitemParse(const string &in url,dictionary &inout meta,array<dictionary> &inout choices){
    meta["title"]="HLS compatibility regression";
    string stream="http://127.0.0.1:18764/ROUTE.m3u8";
    dictionary c={{"url",stream},{"va","va"},{"quality","HLS fixture"},{"itag",1}};
    choices.insertLast(c);return stream;
}
'@
@{schemaVersion=1;mediaScript="$fixtures/HlsResolver.as";statisticsEnabled=$false;chatVisible=$false;volume=0;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax"} | ConvertTo-Json | Set-Content "$data/settings.json" -Encoding utf8
$server = Start-Process -FilePath $Python -ArgumentList @('"'+"$root/tests/hls_http_fixture.py"+'"','--directory','"'+$fixtures+'"','--port','18764') -WindowStyle Hidden -PassThru -RedirectStandardError "$root/.local/hls-http-errors.log"
try {
    Start-Sleep -Milliseconds 600
    if ($server.HasExited) { throw 'Local HLS server failed to start' }
    $resolver.Replace('ROUTE','live') | Set-Content "$fixtures/HlsResolver.as" -Encoding utf8
    & "$root/tools/smoke-test.ps1" -Open 'https://fixture.test/hls' -Seconds $Seconds -DataDirectory $data -DirectNetwork
    $live = Get-Content "$data/smoke-result.json" -Raw | ConvertFrom-Json
    Copy-Item "$data/smoke-result.json" "$root/.local/hls-live-result.json" -Force
    $metrics = Invoke-RestMethod http://127.0.0.1:18764/metrics
    $metrics | ConvertTo-Json | Set-Content "$root/.local/hls-live-http.json" -Encoding utf8
    if ($live.error -or -not $live.hasMedia -or $live.canSeek -or $live.diagnostics.guardedHttpOpens -lt 1 -or $live.diagnostics.rendererFramesDrawn -lt 300 -or $metrics.manifest -lt 2 -or $metrics.connections -lt 3) { throw 'Growing live HLS / compatibility options were not confirmed' }
    $resolver.Replace('ROUTE','broken') | Set-Content "$fixtures/HlsResolver.as" -Encoding utf8
    & "$root/tools/smoke-test.ps1" -Open 'https://fixture.test/hls' -Seconds 16 -DataDirectory $data -DirectNetwork
    Copy-Item "$data/smoke-result.json" "$root/.local/hls-failed-result.json" -Force
    $failedMetrics = Invoke-RestMethod http://127.0.0.1:18764/metrics
    $failedMetrics | ConvertTo-Json | Set-Content "$root/.local/hls-failed-http.json" -Encoding utf8
    if ($failedMetrics.missing -lt 1) { throw 'Missing-segment failure was not exercised' }
    Write-Output 'Live playlist reloads and missing segments completed without process termination.'
} finally {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id }
}
