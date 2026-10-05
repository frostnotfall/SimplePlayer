param([string]$Ffmpeg = 'ffmpeg', [switch]$Bfrc)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fixtures = "$root/.local/fixtures"
$data = "$root/.local/integration-data"
New-Item -ItemType Directory -Force $fixtures,$data | Out-Null
$cue = "1`n00:00:00,000 --> 00:00:12,000`n双字幕 · Primary caption`n`n"
[IO.File]::WriteAllText("$fixtures/primary.srt",$cue,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText("$fixtures/secondary.srt",$cue.Replace('Primary','Secondary'),[Text.UTF8Encoding]::new($false))
& $Ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc2=size=960x540:rate=30' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 12 -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac "$fixtures/muxed.mp4"
if ($LASTEXITCODE) { throw 'Media generation failed' }
& $Ffmpeg -hide_banner -loglevel error -y -i "$fixtures/muxed.mp4" -an -c copy "$fixtures/video.mp4"
if ($LASTEXITCODE) { throw 'Video extraction failed' }
& $Ffmpeg -hide_banner -loglevel error -y -i "$fixtures/muxed.mp4" -vn -c:a aac -b:a 128k "$fixtures/audio.m4a"
if ($LASTEXITCODE) { throw 'Audio extraction failed' }
& $Ffmpeg -hide_banner -loglevel error -y -i "$fixtures/muxed.mp4" -vn -c:a aac -b:a 32k "$fixtures/audio-low.m4a"
if ($LASTEXITCODE) { throw 'Low-bitrate audio generation failed' }
& $Ffmpeg -hide_banner -loglevel error -y -i "$fixtures/muxed.mp4" -i "$fixtures/primary.srt" -c copy -c:s srt -metadata:s:s:0 language=chi "$fixtures/embedded.mkv"
if ($LASTEXITCODE) { throw 'Subtitle fixture generation failed' }
Copy-Item -LiteralPath "$root/tests/FixtureResolver.as" -Destination "$fixtures/FixtureResolver.as"
$settings = @{schemaVersion=1;mediaScript="$fixtures/FixtureResolver.as";statisticsEnabled=$false;audioRendererPath="$root/dist/SimplePlayer/filters/MpcAudioRenderer.ax";bfrcEnabled=[bool]$Bfrc}
$settings | ConvertTo-Json | Set-Content -LiteralPath "$data/settings.json" -Encoding utf8
& "$root/tools/smoke-test.ps1" -Open 'https://fixture.test/dual' -DataDirectory $data -Seconds 12 -Exercise -PrimarySubtitle "$fixtures/primary.srt" -SecondarySubtitle "$fixtures/secondary.srt"
$dual = Get-Content "$data/smoke-result.json" -Raw | ConvertFrom-Json
Copy-Item "$data/smoke-result.json" "$root/.local/dual-stream-result.json" -Force
foreach ($event in @('opened','paused','seeked','resumed','switched','audioSwitched','audioRestored','failedStreamRecovered','stopped','reopened')) { if ($dual.verification -notcontains $event) { throw "Dual-stream action did not pass: $event" } }
$subtitleEvidence = @($dual.verification | Where-Object { $_.beforeStop })[0].beforeStop
if ($subtitleEvidence.sourceCount -ne 2 -or $subtitleEvidence.primarySubtitleFrames -lt 1 -or $subtitleEvidence.secondarySubtitleFrames -lt 1) { throw 'Dual-stream / external subtitle rendering was not confirmed' }
if ($dual.diagnostics.audioBitrateBps -ne 128000 -or $dual.diagnostics.selectedAudioId -ne 'itag:3' -or -not $dual.diagnostics.scriptDefaultEntry -or -not $dual.diagnostics.automaticAudioSelection) { throw 'Script default video / highest-bitrate automatic audio selection was not confirmed' }
& "$root/tools/smoke-test.ps1" -Open "$fixtures/embedded.mkv" -DataDirectory $data -Seconds 12 -Exercise -EmbeddedMain -SecondarySubtitle "$fixtures/secondary.srt"
$embedded = Get-Content "$data/smoke-result.json" -Raw | ConvertFrom-Json
Copy-Item "$data/smoke-result.json" "$root/.local/embedded-result.json" -Force
foreach ($event in @('opened','paused','seeked','resumed','switched','stopped','reopened')) { if ($embedded.verification -notcontains $event) { throw "Embedded-subtitle action did not pass: $event" } }
$subtitleEvidence = @($embedded.verification | Where-Object { $_.beforeStop })[0].beforeStop
if (-not $subtitleEvidence.embeddedSubtitle -or $subtitleEvidence.primarySubtitleFrames -lt 1 -or $subtitleEvidence.secondarySubtitleFrames -lt 1) { throw 'Embedded text subtitle rendering was not confirmed' }
Write-Output 'Dual sources, WASAPI, two external subtitles, embedded+external, seek/switch/stop/reopen passed.'
