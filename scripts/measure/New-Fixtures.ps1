# Generates the test videos both clients are measured with (measurement
# protocol rev. 2): 60-second loops of a moving test pattern with a tone,
# 1080p60 and 2160p60, each in H.264 and HEVC (8-bit SDR), so hardware decoding
# is exercised. Adds their lines to SHA256SUMS next to them. Needs ffmpeg (path or on PATH).
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$OutDir,
    [string]$Ffmpeg = 'ffmpeg',
    [int]$Seconds = 60
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$h264 = @('-c:v', 'libx264', '-preset', 'veryfast', '-crf', '20', '-pix_fmt', 'yuv420p')
$hevc = @('-c:v', 'libx265', '-preset', 'ultrafast', '-crf', '24', '-pix_fmt', 'yuv420p', '-tag:v', 'hvc1')
$fixtures = @(
    @{ Name = 'pattern-1080p60-h264.mkv'; Size = '1920x1080'; Codec = $h264 },
    @{ Name = 'pattern-1080p60-hevc.mkv'; Size = '1920x1080'; Codec = $hevc },
    @{ Name = 'pattern-2160p60-h264.mkv'; Size = '3840x2160'; Codec = $h264 },
    @{ Name = 'pattern-2160p60-hevc.mkv'; Size = '3840x2160'; Codec = $hevc }
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
# sha256sum format, so the files can be checked anywhere.
$sums = Get-ChildItem -LiteralPath $OutDir -Filter 'pattern-*.mkv' | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
}
# Keep the lines of other files in the folder (for example clips the 2.x
# harness added); replace only the pattern files' lines.
$sumsPath = Join-Path $OutDir 'SHA256SUMS'
$others = @()
if (Test-Path -LiteralPath $sumsPath) {
    $others = @(Get-Content -LiteralPath $sumsPath | Where-Object { $_ -and $_ -notmatch '^[0-9a-f]{64}  pattern-[^ ]*\.mkv$' })
}
[IO.File]::WriteAllText($sumsPath, ((@($others) + @($sums)) -join "`n") + "`n", (New-Object Text.UTF8Encoding $false))
$sums
