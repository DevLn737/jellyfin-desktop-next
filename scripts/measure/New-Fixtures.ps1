# Generates the test videos both clients are measured with: 60-second loops of
# a moving test pattern with a tone, 1080p60 H.264 and 2160p60 HEVC (8-bit SDR),
# so hardware decoding is exercised. Needs ffmpeg (path or on PATH).
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$OutDir,
    [string]$Ffmpeg = 'ffmpeg',
    [int]$Seconds = 60
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$fixtures = @(
    @{ Name = 'pattern-1080p60-h264.mkv'; Size = '1920x1080'; Codec = @('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '20', '-pix_fmt', 'yuv420p') },
    @{ Name = 'pattern-2160p60-hevc.mkv'; Size = '3840x2160'; Codec = @('-c:v', 'libx265', '-preset', 'ultrafast', '-crf', '24', '-pix_fmt', 'yuv420p', '-tag:v', 'hvc1') }
)
foreach ($f in $fixtures) {
    $target = Join-Path $OutDir $f.Name
    if (Test-Path -LiteralPath $target) { Write-Host "exists: $target"; continue }
    Write-Host "generating $target"
    $arguments = @('-hide_banner', '-loglevel', 'error', '-y',
        '-f', 'lavfi', '-i', "testsrc2=size=$($f.Size):rate=60",
        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
        '-t', "$Seconds") + $f.Codec + @('-g', '120', '-c:a', 'aac', '-b:a', '128k', $target)
    & $Ffmpeg @arguments
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for $($f.Name)" }
}
Get-ChildItem -LiteralPath $OutDir -Filter 'pattern-*' | ForEach-Object {
    [pscustomobject]@{ name = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
} | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $OutDir 'fixtures.json')
