# Runs the phase 1 weeks 1-2 measurements of the prototype on this computer and
# writes one report (summary.json, the per-run reports and a screen recording).
# Run it while logged in locally (not over Remote Desktop) with the computer
# otherwise idle. See README.md in this folder.
[CmdletBinding()]
param(
    # Folder with jellyfin-desktop-next.exe (the CI artifact, unpacked).
    [Parameter(Mandatory)] [string]$AppDir,
    [Parameter(Mandatory)] [string]$OutDir,
    # Folder with pattern-1080p60-h264.mkv and pattern-2160p60-hevc.mkv (New-Fixtures.ps1).
    [Parameter(Mandatory)] [string]$FixtureDir,
    [string]$Ffmpeg = 'ffmpeg',
    # Short durations to check that everything runs; numbers are not valid.
    [switch]$Quick,
    # Runs to do; default all.
    [string[]]$Only = @('frames', 'seek', 'fullscreen', 'shutdown', 'recording', 'memory')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Import-Module (Join-Path $PSScriptRoot 'MeasureCommon.psm1') -Force

$exe = Join-Path $AppDir 'jellyfin-desktop-next.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "Not found: $exe" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$video1080 = (Resolve-Path (Join-Path $FixtureDir 'pattern-1080p60-h264.mkv')).Path
$video2160 = (Resolve-Path (Join-Path $FixtureDir 'pattern-2160p60-hevc.mkv')).Path

$framesSeconds = if ($Quick) { 20 } else { 300 }
$memorySeconds = if ($Quick) { 120 } else { 3600 }
$memoryWarmup = if ($Quick) { 20 } else { 120 }
$toggles = if ($Quick) { 5 } else { 50 }
$shutdownRuns = if ($Quick) { 2 } else { 5 }

$summary = [ordered]@{
    schemaVersion = 1
    client = 'jellyfin-desktop-next'
    startedAt = (Get-Date).ToUniversalTime().ToString('o')
    quick = [bool]$Quick
    computer = [ordered]@{
        os = (Get-CimInstance Win32_OperatingSystem).Caption
        cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
        gpus = @(Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) (driver $($_.DriverVersion))" })
        remoteSession = [bool]$env:SESSIONNAME -and $env:SESSIONNAME -like 'RDP*'
    }
    runs = [ordered]@{}
}
if ($summary.computer.remoteSession) {
    Write-Warning 'This is a Remote Desktop session: GPU and frame numbers will not be representative.'
}

function Invoke-App([string]$Name, [string[]]$Arguments, [int]$TimeoutSeconds) {
    $report = Join-Path $OutDir "$Name.json"
    $log = Join-Path $OutDir "$Name.log"
    Remove-Item -LiteralPath $report -ErrorAction SilentlyContinue
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] $Name"
    $all = @('--report', "`"$report`"") + $Arguments
    $process = Start-Process -FilePath $exe -ArgumentList $all -PassThru -RedirectStandardError $log
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw "$Name did not finish in $TimeoutSeconds s"
    }
    $result = [ordered]@{ exitCode = $process.ExitCode }
    if (Test-Path -LiteralPath $report) { $result.report = Get-Content -Raw $report | ConvertFrom-Json }
    return $result
}

# 1. Frames: animated menu over 1080p60 and 2160p60 in fullscreen, hardware decoding.
if ($Only -contains 'frames') {
    foreach ($case in @(@{ Name = 'frames-1080p60'; File = $video1080 }, @{ Name = 'frames-2160p60'; File = $video2160 })) {
        $summary.runs[$case.Name] = Invoke-App $case.Name @('--scenario', 'frames', '--fullscreen', '--animate-menu',
            '--warmup', '10', '--duration', "$framesSeconds", "`"$($case.File)`"") ($framesSeconds + 90)
    }
    # The same without hardware decoding, for the CPU comparison.
    $summary.runs['frames-1080p60-software'] = Invoke-App 'frames-1080p60-software' @('--scenario', 'frames', '--fullscreen',
        '--animate-menu', '--hwdec', 'no', '--warmup', '10', '--duration', '60', "`"$video1080`"") 150
}

# 2. Seek: 20 exact seeks over the 1080p file.
if ($Only -contains 'seek') {
    $summary.runs['seek'] = Invoke-App 'seek' @('--scenario', 'seek', '--seeks', '20', "`"$video1080`"") 180
}

# 3. Fullscreen: round trips, spread over all monitors.
if ($Only -contains 'fullscreen') {
    $summary.runs['fullscreen'] = Invoke-App 'fullscreen' @('--scenario', 'fullscreen', '--toggles', "$toggles",
        "`"$video1080`"") ($toggles * 4 + 60)
}

# 4. Shutdown: close the window like a user during playback with the menu animating.
if ($Only -contains 'shutdown') {
    $runs = @()
    for ($i = 1; $i -le $shutdownRuns; $i++) {
        $log = Join-Path $OutDir "shutdown-$i.log"
        $process = Start-Process -FilePath $exe -ArgumentList @('--scenario', 'play', '--animate-menu', "`"$video1080`"") -PassThru -RedirectStandardError $log
        if (-not (Wait-MainWindow $process 30)) { throw 'The window did not appear' }
        Start-Sleep -Seconds 15
        $runs += Stop-ByClosingWindow $process
        Start-Sleep -Seconds 2
    }
    $summary.runs['shutdown'] = [ordered]@{
        runs = $runs
        maxMilliseconds = ($runs | Measure-Object milliseconds -Maximum).Maximum
        allExitCodesZero = -not ($runs | Where-Object { $_.exitCode -ne 0 })
    }
}

# 5. Screen recording for the pull request (separate run: recording costs GPU time).
if ($Only -contains 'recording') {
    $recording = Join-Path $OutDir 'recording.mp4'
    $process = Start-Process -FilePath $exe -ArgumentList @('--scenario', 'play', '--stats', "`"$video1080`"") -PassThru
    if (-not (Wait-MainWindow $process 30)) { throw 'The window did not appear' }
    Start-Sleep -Seconds 2
    $title = $process.MainWindowTitle
    $ffmpegArguments = @('-hide_banner', '-loglevel', 'error', '-y', '-f', 'gdigrab', '-framerate', '30', '-t', '40',
        '-i', 'desktop', '-vf', 'scale=1280:-2', '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '26', '-pix_fmt', 'yuv420p', $recording)
    $recorder = Start-Process -FilePath $Ffmpeg -ArgumentList $ffmpegArguments -PassThru -NoNewWindow
    # Drive the window like a user: open the menu, go fullscreen, back.
    $shell = New-Object -ComObject WScript.Shell
    foreach ($step in @(@{ Key = 'm'; Wait = 6 }, @{ Key = 'f'; Wait = 6 }, @{ Key = 'm'; Wait = 6 }, @{ Key = 'm'; Wait = 4 }, @{ Key = 'f'; Wait = 6 }, @{ Key = ' '; Wait = 3 }, @{ Key = ' '; Wait = 4 })) {
        [void]$shell.AppActivate($process.Id)
        Start-Sleep -Milliseconds 300
        $shell.SendKeys($step.Key)
        Start-Sleep -Seconds $step.Wait
    }
    [void]$recorder.WaitForExit(30000)
    $null = Stop-ByClosingWindow $process
    $summary.runs['recording'] = [ordered]@{ file = 'recording.mp4'; windowTitle = $title }
}

# 6. Memory: one hour with the menu animating; the file is replaced every 60 s
# (1080p and 2160p alternately), sampled inside and outside the process.
if ($Only -contains 'memory') {
    $report = Join-Path $OutDir 'memory.json'
    Write-Host "[$((Get-Date).ToString('HH:mm:ss'))] memory ($memorySeconds s)"
    $process = Start-Process -FilePath $exe -PassThru -RedirectStandardError (Join-Path $OutDir 'memory.log') -ArgumentList @(
        '--report', "`"$report`"", '--scenario', 'memory', '--animate-menu', '--duration', "$memorySeconds",
        '--warmup', "$memoryWarmup", '--sample-interval', '5', '--replace-interval', '60', "`"$video1080`"", "`"$video2160`"")
    $outside = Measure-ProcessTree -Process $process -DurationSeconds ($memorySeconds + 120) -IntervalSeconds 5 -WarmupSeconds $memoryWarmup
    [void]$process.WaitForExit(120000)
    $summary.runs['memory'] = [ordered]@{
        exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
        report = if (Test-Path -LiteralPath $report) { Get-Content -Raw $report | ConvertFrom-Json } else { $null }
        outside = [ordered]@{ privateGrowthBytes = $outside.privateGrowthBytes; privatePeakBytes = $outside.privatePeakBytes; cpuPercentOfOneCore = $outside.cpuPercentOfOneCore }
    }
}

# Thresholds (docs/roadmap.md). Each is pass / fail / not measured.
function Verdict($condition) { if ($null -eq $condition) { 'not measured' } elseif ($condition) { 'pass' } else { 'fail' } }
$t = [ordered]@{}
foreach ($name in @('frames-1080p60', 'frames-2160p60')) {
    if ($summary.runs.Contains($name) -and $summary.runs[$name].report) {
        $f = $summary.runs[$name].report.frames
        $t["$name p95 <= 1.25 periods"] = [ordered]@{ value = [math]::Round($f.p95Periods, 3); verdict = Verdict ($f.p95Periods -le 1.25) }
        $t["$name avsync < 50 ms"] = [ordered]@{ value = $f.avsyncAbsMsP95; verdict = Verdict ($f.avsyncAbsMsP95 -lt 50) }
        $hw = @($summary.runs[$name].report.hwdecCurrent) -join ','
        $t["$name hardware decoding"] = [ordered]@{ value = $hw; verdict = Verdict ($hw -ne '' -and $hw -ne 'no') }
    }
}
if ($summary.runs.Contains('frames-1080p60') -and $summary.runs['frames-1080p60'].report) {
    $first = @($summary.runs['frames-1080p60'].report.firstFrameMs)[0]
    $t['first frame < 2 s (local file)'] = [ordered]@{ value = $first; verdict = Verdict ($first -lt 2000) }
}
if ($summary.runs.Contains('seek') -and $summary.runs['seek'].report) {
    $s = $summary.runs['seek'].report.seek
    $t['seek < 1 s (max)'] = [ordered]@{ value = $s.maxMs; verdict = Verdict ($s.maxMs -lt 1000 -and $s.failures -eq 0) }
}
if ($summary.runs.Contains('fullscreen') -and $summary.runs['fullscreen'].report) {
    $fs = $summary.runs['fullscreen'].report.fullscreen
    $t['fullscreen without drift'] = [ordered]@{ value = "$($fs.roundTrips) round trips, $($fs.geometryDrifts) drifts, $($fs.fullscreenMismatches) mismatches"; verdict = Verdict ($fs.roundTrips -eq $toggles -and $fs.geometryDrifts -eq 0 -and $fs.fullscreenMismatches -eq 0) }
}
if ($summary.runs.Contains('shutdown')) {
    $sd = $summary.runs['shutdown']
    $t['shutdown < 1 s, exit code 0'] = [ordered]@{ value = [math]::Round($sd.maxMilliseconds); verdict = Verdict ($sd.maxMilliseconds -lt 1000 -and $sd.allExitCodesZero) }
}
if ($summary.runs.Contains('memory') -and $summary.runs['memory'].report) {
    $growth = $summary.runs['memory'].report.memory.privateGrowthBytes
    $t['memory growth <= 64 MiB per hour'] = [ordered]@{ value = [math]::Round($growth / 1MB, 1); verdict = Verdict ((-not $Quick) -and $growth -le 64MB) }
}
$summary.thresholds = $t
$summary.finishedAt = (Get-Date).ToUniversalTime().ToString('o')
$summary | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 (Join-Path $OutDir 'summary.json')
$t.GetEnumerator() | ForEach-Object { '{0,-40} {1,-12} {2}' -f $_.Key, $_.Value.verdict, $_.Value.value } | Write-Host
Write-Host "Report: $(Join-Path $OutDir 'summary.json')"
