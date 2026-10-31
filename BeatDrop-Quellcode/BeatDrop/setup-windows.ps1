# BeatDrop - einmaliges Setup fuer Windows
# Laedt yt-dlp, ffmpeg und deno nach %APPDATA%\BeatDrop\bin.
# Ausfuehren: Rechtsklick auf die Datei -> "Mit PowerShell ausfuehren"

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest ist sonst extrem langsam

$bin = Join-Path $env:APPDATA "BeatDrop\bin"
$tmp = Join-Path $env:TEMP "beatdrop-setup"
New-Item -ItemType Directory -Force -Path $bin | Out-Null
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

function Get-File($url, $out) {
    Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing
}

Write-Host "1/3  yt-dlp ..."
Get-File "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" (Join-Path $bin "yt-dlp.exe")

Write-Host "2/3  ffmpeg + ffprobe ..."
$ffZip = Join-Path $tmp "ffmpeg.zip"
Get-File "https://github.com/yt-dlp/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-win64-gpl.zip" $ffZip
Expand-Archive -Path $ffZip -DestinationPath (Join-Path $tmp "ffmpeg") -Force
Get-ChildItem (Join-Path $tmp "ffmpeg") -Recurse -Include "ffmpeg.exe", "ffprobe.exe" |
    Copy-Item -Destination $bin -Force

Write-Host "3/3  deno (JavaScript-Runtime, die yt-dlp fuer YouTube braucht) ..."
$denoZip = Join-Path $tmp "deno.zip"
Get-File "https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip" $denoZip
Expand-Archive -Path $denoZip -DestinationPath $bin -Force

Remove-Item $tmp -Recurse -Force

Write-Host ""
Write-Host "Fertig. Alles liegt in: $bin"
Write-Host -NoNewline "yt-dlp Version: "
& (Join-Path $bin "yt-dlp.exe") --version
Write-Host ""
Write-Host "Du kannst das Fenster schliessen."
Read-Host "Enter druecken zum Beenden"
