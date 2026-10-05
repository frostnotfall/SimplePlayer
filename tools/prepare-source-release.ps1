param([string]$Version='0.1.0',[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
if($Version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$'){throw 'Invalid version'}
$root=Split-Path -Parent $PSScriptRoot
$files=@(& git -C $root ls-files --cached --others --exclude-standard) | Sort-Object -Unique
if($LASTEXITCODE){throw 'Cannot enumerate Git source files'}
$files=@($files | Where-Object {$_ -match '^(player/|tests/|tools/|packaging/|\.github/|\.gitignore$|CMakeLists\.txt$|CMakePresets\.json$|LICENSE$|README\.md$|CONTRIBUTING\.md$|SECURITY\.md$)'})
$files=@($files | Where-Object {$_ -ne 'packaging/RELEASE-v0.1.0.md'})
if(-not $files.Count){throw 'No source files found'}
$violations=[Collections.Generic.List[string]]::new()
foreach($relative in $files){
    $full=Join-Path $root $relative
    $info=Get-Item -LiteralPath $full
    if($info.Length -gt 10MB){$violations.Add("Large source file: $relative")}
    if($relative -match '(?i)(^|/)(data|webview2|extensions|\.local|\.deps|dist|build)/' -and $relative -notmatch '^player/src/accounts/webview2/'){$violations.Add("Private/runtime directory: $relative")}
    if($relative -match '(?i)\.(exe|dll|ax|pdb|dmp|log|mp4|mkv|flv|zip|7z)$'){$violations.Add("Binary/runtime file: $relative")}
    $text=[IO.File]::ReadAllText($full)
    if($text -match '(?:ghp_[A-Za-z0-9]{36}|github_pat_[A-Za-z0-9_]{60,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----)'){$violations.Add("Credential pattern: $relative")}
    if($text -match '(?i)[A-Z]:[/\\](?:Users[/\\][^/\\]+|49340[/\\])'){$violations.Add("Personal absolute path: $relative")}
}
if($violations.Count){$violations | ForEach-Object {Write-Output $_};throw 'Source release review failed; no archive generated'}
$output=if(-not $OutputDirectory){Join-Path $root 'release'}elseif([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $root $OutputDirectory}
$output=[IO.Path]::GetFullPath($output)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$archive=Join-Path $output "SimplePlayer-$Version-source.zip"
if(Test-Path -LiteralPath $archive){throw 'Archive already exists; choose another version or move the previous archive aside'}
Add-Type -AssemblyName System.IO.Compression
$stream=[IO.File]::Open($archive,[IO.FileMode]::CreateNew)
$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
try{
    foreach($relative in $files){
        $entry=$zip.CreateEntry("SimplePlayer-$Version/$relative",[IO.Compression.CompressionLevel]::Optimal)
        $input=[IO.File]::OpenRead((Join-Path $root $relative));$target=$entry.Open()
        try{$input.CopyTo($target)}finally{$input.Dispose();$target.Dispose()}
    }
}finally{$zip.Dispose();$stream.Dispose()}
$sha=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$sha  $(Split-Path -Leaf $archive)" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS-source.txt') -Encoding ascii
@{version=$Version;archive=(Split-Path -Leaf $archive);sha256=$sha;fileCount=$files.Count;files=$files;scope='Source snapshot; excludes dependencies, binaries, user data and Git history'} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'source-release-manifest.json') -Encoding utf8
Write-Output "Source release ready: $($files.Count) files, $((Get-Item -LiteralPath $archive).Length) bytes"
Write-Output $archive
