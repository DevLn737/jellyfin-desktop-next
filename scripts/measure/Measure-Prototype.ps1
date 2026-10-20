<#
Stage 1 measurements of the prototype on a real Windows PC, following the
project's architecture/measurement-protocol.md (rev. 2). Same scenarios,
timings, statistics and mpv probe as the 2.x baseline harness
(scripts/baseline/Measure-Baseline.ps1 of DevLn737/jellyfin-desktop), so the
two reports compare directly. Local media only, never a server.

The app is driven over mpv's JSON IPC (a named pipe): loadfile, seek, probe
marks and the app's own script messages (jfd-fullscreen). Frames come from
the Qt Quick render loop log, like the baseline.

Example:
  powershell -ExecutionPolicy Bypass -File Measure-Prototype.ps1 `
    -AppDir C:\jfd\next -MediaDir C:\Users\win\Documents\Claude\jfd-media -OutDir C:\jfd\next-out
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$AppDir,
    [Parameter(Mandatory)][string]$MediaDir,
    [Parameter(Mandatory)][string]$OutDir,
    [string[]]$Scenarios = @('frames-1080','frames-2160','cpu-1080','cpu-2160','first-frame-seek','fullscreen-50','shutdown','memory-hour','recording'),
    # auto-safe is the app's default (direct decoding where safe); auto-copy matches the fork's default mode.
    [string[]]$HardwareDecoding = @('auto-safe','auto-copy'),
    [int]$MemoryMeasureSeconds = 3600,
    [int]$MemoryWarmupSeconds = 120,
    [string]$PresentMonPath,
    # File names in MediaDir; frames/cpu run once per file, the first one is also used elsewhere.
    [string[]]$Media1080 = @('pattern-1080p60-h264.mkv','pattern-1080p60-hevc.mkv'),
    [string[]]$Media2160 = @('pattern-2160p60-h264.mkv','pattern-2160p60-hevc.mkv'),
    [int]$FrameWindowSeconds = 300,
    [int]$CpuWindowSeconds = 60,
    [int]$Seeks = 20,
    # Used only by the recording scenario (gdigrab).
    [string]$Ffmpeg = 'ffmpeg',
    [int]$RecordingSeconds = 30
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# `powershell -File` passes "a,b" as one string; accept both forms.
$Scenarios = @($Scenarios | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$HardwareDecoding = @($HardwareDecoding | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Media1080 = @($Media1080 | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Media2160 = @($Media2160 | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$probeScript = Join-Path $PSScriptRoot 'jfd-probe.lua'
$appRoot = [IO.Path]::GetFullPath($AppDir)
$media = [IO.Path]::GetFullPath($MediaDir)
$out = [IO.Path]::GetFullPath($OutDir)
$exeName = 'jellyfin-desktop-next.exe'
$appExe = Join-Path $appRoot $exeName
if (-not (Test-Path -LiteralPath $appExe)) { throw "AppDir must contain $exeName" }
$mediaLists = @{ '1080' = @($Media1080 | ForEach-Object { Join-Path $media $_ }); '2160' = @($Media2160 | ForEach-Object { Join-Path $media $_ }) }
$mediaFiles = @{ '1080' = $mediaLists['1080'][0]; '2160' = $mediaLists['2160'][0] }
foreach ($file in ($mediaLists['1080'] + $mediaLists['2160'])) { if (-not (Test-Path -LiteralPath $file)) { throw "Missing media $file" } }
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Write-Json($Value, [string]$Path) {
    [IO.File]::WriteAllText($Path, (($Value | ConvertTo-Json -Depth 12) + "`n"), (New-Object Text.UTF8Encoding $false))
}
function Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Log([string]$Text) { $line = "[{0:HH:mm:ss}] {1}" -f (Get-Date), $Text; Write-Host $line; Add-Content -LiteralPath (Join-Path $out 'harness.log') -Value $line }

if (-not ('JfdWin' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class JfdWin {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct DEVMODE {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; public int dmFields;
        public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels; public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern int GetWindowTextLength(IntPtr hwnd);
    [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool EnumDisplaySettings(string name, int mode, ref DEVMODE dm);
    public static IntPtr Find(int pid) {
        IntPtr best = IntPtr.Zero; long bestArea = 0;
        EnumWindows((h, p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if (owner == (uint)pid && IsWindowVisible(h) && GetWindowTextLength(h) > 0) {
                RECT r; GetWindowRect(h, out r);
                long area = (long)(r.Right - r.Left) * (r.Bottom - r.Top);
                if (area > bestArea) { bestArea = area; best = h; }
            }
            return true;
        }, IntPtr.Zero);
        return best;
    }
    public static int[] Rect(IntPtr h) { RECT r; GetWindowRect(h, out r); return new[] { r.Left, r.Top, r.Right, r.Bottom }; }
    public static int Refresh(string device) {
        var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
        return EnumDisplaySettings(device, -1, ref dm) ? dm.dmDisplayFrequency : 0;
    }
}
'@
}
Add-Type -AssemblyName System.Windows.Forms

function Get-Inventory {
    $screens = @([Windows.Forms.Screen]::AllScreens | ForEach-Object {
        @{ device = $_.DeviceName; primary = $_.Primary; bounds = @($_.Bounds.X, $_.Bounds.Y, $_.Bounds.Width, $_.Bounds.Height)
           refreshHz = [JfdWin]::Refresh($_.DeviceName) } })
    $os = Get-CimInstance Win32_OperatingSystem
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    @{
        computer = $env:COMPUTERNAME
        os = "$($os.Caption) $($os.Version)"
        cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
        logicalProcessors = [Environment]::ProcessorCount
        ramBytes = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
        gpus = @(Get-CimInstance Win32_VideoController | ForEach-Object { @{ name = $_.Name; driver = $_.DriverVersion } })
        screens = $screens
        elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
        sessionName = $env:SESSIONNAME
        remoteSession = [Windows.Forms.SystemInformation]::TerminalServerSession
        powerPlan = ((powercfg /getactivescheme) -join ' ')
    }
}

# ---------------------------------------------------------------- app control
$script:app = $null; $script:window = [IntPtr]::Zero; $script:pipe = $null; $script:reader = $null; $script:writer = $null
$script:requestId = 0; $script:pending = $null; $script:events = New-Object 'System.Collections.Generic.List[object]'

# Overlay: 'protocol' is the panel, spinner and text of the measurement protocol; 'none' is video only.
function Start-Client([string]$Hwdec, [bool]$RenderLog, [string]$RunDir, [string]$Overlay, [string]$File, [string[]]$Extra = @()) {
    $probe = $probeScript.Replace('\','/'); $probeOut = (Join-Path $RunDir 'mpv-probe.jsonl').Replace('\','/')
    $pipeName = "jfd-measure-$PID-$(Get-Random)"
    $arguments = @('--maximized', '--overlay', $Overlay, '--hwdec', $Hwdec, '--log-file', ('"' + (Join-Path $RunDir 'client.log') + '"'),
        '--mpv-option', 'mute=yes', '--mpv-option', 'loop-file=inf',
        '--mpv-option', ('"script=' + $probe + '"'), '--mpv-option', ('"script-opts=jfdprobe-out=' + $probeOut + '"'),
        '--mpv-option', "input-ipc-server=\\.\pipe\$pipeName") + $Extra + @(('"' + $File + '"'))
    $env:QT_LOGGING_RULES = if ($RenderLog) { 'qt.scenegraph.time.renderloop=true' } else { $null }
    $script:app = Start-Process -FilePath $appExe -WorkingDirectory $appRoot -PassThru -ArgumentList $arguments `
        -RedirectStandardOutput (Join-Path $RunDir 'stdout.txt') -RedirectStandardError (Join-Path $RunDir 'stderr.txt')
    $null = $script:app.Handle
    $env:QT_LOGGING_RULES = $null
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 200
        if ($script:app.HasExited) { throw "Client exited during startup (code $($script:app.ExitCode))" }
        $script:window = [JfdWin]::Find($script:app.Id)
    } while ($script:window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($script:window -eq [IntPtr]::Zero) { throw 'Client window did not appear' }
    Connect-Ipc $pipeName
    $null = [JfdWin]::ShowWindow($script:window, 3)
    # The app starts playback itself once its render context exists, possibly
    # before the pipe was connected, so poll instead of waiting for an event.
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        $playing = $false
        try { $playing = ($null -ne (Invoke-Mpv @('get_property', 'time-pos'))) -and -not (Invoke-Mpv @('get_property', 'paused-for-cache')) } catch {}
        if ($playing) { break }
        if ($clock.Elapsed.TotalSeconds -gt 30) { throw 'Playback did not start' }
        Start-Sleep -Milliseconds 100
    }
    return @{ playingAfterWindowMs = $clock.Elapsed.TotalMilliseconds }
}

function Connect-Ipc([string]$Name) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ($true) {
        $pipe = New-Object IO.Pipes.NamedPipeClientStream('.', $Name, [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
        try { $pipe.Connect(1000); break } catch { $pipe.Dispose(); if ([DateTime]::UtcNow -gt $deadline) { throw 'mpv IPC pipe did not appear' } }
    }
    $script:pipe = $pipe
    $script:reader = New-Object IO.StreamReader($pipe, (New-Object Text.UTF8Encoding $false))
    $script:writer = New-Object IO.StreamWriter($pipe, (New-Object Text.UTF8Encoding $false))
    $script:writer.AutoFlush = $true
    $script:pending = $null
    $script:events.Clear()
}

# Reads one IPC line; $null on timeout. A pending read survives a timeout and is reused.
function Read-IpcLine([int]$TimeoutMs) {
    if (-not $script:pending) { $script:pending = $script:reader.ReadLineAsync() }
    if (-not $script:pending.Wait($TimeoutMs)) { return $null }
    $line = $script:pending.Result; $script:pending = $null
    if ($null -eq $line) { throw 'mpv IPC pipe closed' }
    return ($line | ConvertFrom-Json)
}

function Invoke-Mpv([object[]]$Command, [int]$TimeoutMs = 10000) {
    $id = ++$script:requestId
    $script:writer.WriteLine((@{ command = $Command; request_id = $id } | ConvertTo-Json -Compress -Depth 4))
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        $message = Read-IpcLine ([int][Math]::Max(1, ($deadline - [DateTime]::UtcNow).TotalMilliseconds))
        if (-not $message) { break }
        if ($message.PSObject.Properties['event']) { $script:events.Add($message); continue }
        if ($message.PSObject.Properties['request_id'] -and $message.request_id -eq $id) {
            if ($message.error -ne 'success') { throw "mpv command $($Command -join ' ') failed: $($message.error)" }
            if ($message.PSObject.Properties['data']) { return $message.data }
            return $null
        }
    }
    throw "mpv IPC timeout for $($Command -join ' ')"
}

# Waits for the next mpv event of that name (one already queued counts).
function Wait-MpvEvent([string]$Name, [int]$TimeoutMs = 20000) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $script:events.Count; ++$i) {
        if ($script:events[$i].event -eq $Name) { $script:events.RemoveRange(0, $i + 1); return @{ waitedMs = 0 } }
    }
    $script:events.Clear()
    while ($clock.ElapsedMilliseconds -lt $TimeoutMs) {
        $message = Read-IpcLine ([int][Math]::Max(1, $TimeoutMs - $clock.ElapsedMilliseconds))
        if (-not $message) { break }
        if ($message.PSObject.Properties['event'] -and $message.event -eq $Name) { return @{ waitedMs = $clock.Elapsed.TotalMilliseconds } }
    }
    throw "mpv event $Name did not arrive"
}

function Mark([string]$Label) { $null = Invoke-Mpv @('script-message', 'jfdprobe-mark', $Label); return [DateTime]::Now }

function Load-Media([string]$Path) {
    $script:events.Clear()
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $null = Invoke-Mpv @('loadfile', $Path, 'replace')
    $null = Wait-MpvEvent 'start-file' 10000
    $null = Wait-MpvEvent 'playback-restart' 20000
    return @{ readyMs = $clock.Elapsed.TotalMilliseconds }
}

function Close-Ipc {
    foreach ($item in @($script:writer, $script:reader, $script:pipe)) { try { if ($item) { $item.Dispose() } } catch {} }
    $script:writer = $null; $script:reader = $null; $script:pipe = $null; $script:pending = $null
}

function Stop-Client([int]$TimeoutMs = 15000) {
    if (-not $script:app -or $script:app.HasExited) { Close-Ipc; return $null }
    $tree = Get-Tree $script:app.Id
    Close-Ipc
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $null = [JfdWin]::PostMessage($script:window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $exited = $script:app.WaitForExit($TimeoutMs)
    $mainMs = $clock.Elapsed.TotalMilliseconds
    $remaining = @()
    if ($exited) {
        $deadline = [DateTime]::UtcNow.AddMilliseconds(5000)
        do { $remaining = @($tree | Where-Object { $_ -ne $script:app.Id -and (Get-Process -Id $_ -ErrorAction SilentlyContinue) }); if ($remaining) { Start-Sleep -Milliseconds 20 } }
        while ($remaining -and [DateTime]::UtcNow -lt $deadline)
    }
    $treeMs = $clock.Elapsed.TotalMilliseconds
    $result = @{ exited = $exited; mainExitMs = [Math]::Round($mainMs, 1); treeExitMs = [Math]::Round($treeMs, 1)
                 exitCode = $(if ($exited) { $script:app.ExitCode } else { $null }); leftoverChildren = $remaining.Count }
    if (-not $exited) {
        Log 'Client did not exit; terminating the owned process tree'
        foreach ($processId in $tree) { Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue }
    }
    foreach ($processId in $remaining) { Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue }
    $script:app = $null
    Start-Sleep -Seconds 2
    return $result
}

# Owned process tree: the client and its descendants started from the app directory.
function Get-Tree([int]$RootId) {
    $all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, ExecutablePath)
    $members = New-Object 'System.Collections.Generic.HashSet[int]'; $null = $members.Add($RootId)
    do {
        $added = $false
        foreach ($p in $all) {
            if ($members.Contains([int]$p.ParentProcessId) -and $p.ExecutablePath -and
                $p.ExecutablePath.StartsWith($appRoot, [StringComparison]::OrdinalIgnoreCase) -and $members.Add([int]$p.ProcessId)) { $added = $true }
        }
    } while ($added)
    return @($members)
}

function Sample-Tree([int[]]$Ids) {
    $private = 0L; $rss = 0L; $cpu = 0.0; $count = 0
    foreach ($processId in $Ids) {
        $p = Get-Process -Id $processId -ErrorAction SilentlyContinue
        if (-not $p) { continue }
        $private += $p.PrivateMemorySize64; $rss += $p.WorkingSet64; $cpu += $p.TotalProcessorTime.TotalSeconds; ++$count
    }
    return @{ at = [DateTime]::Now; privateBytes = $private; rssBytes = $rss; cpuSeconds = $cpu; processes = $count }
}

# ---------------------------------------------------------------- statistics (same as the baseline harness)
function Percentile([double[]]$Values, [double]$P) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = [double[]]($Values | Sort-Object)
    $index = [Math]::Min($sorted.Count - 1, [Math]::Max(0, [int][Math]::Ceiling($P / 100 * $sorted.Count) - 1))
    return $sorted[$index]
}
function Median([double[]]$Values) { Percentile $Values 50 }

function Read-Probe([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    return @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json })
}
function Probe-Window($Records, [string]$From, [string]$To) {
    $start = ($Records | Where-Object { $_.kind -eq 'mark' -and $_.label -eq $From } | Select-Object -Last 1).t
    $end = ($Records | Where-Object { $_.kind -eq 'mark' -and $_.label -eq $To } | Select-Object -Last 1).t
    return @($Records | Where-Object { $_.kind -eq 'sample' -and $_.t -ge $start -and $_.t -le $end })
}
# The probe omits properties mpv reports as unavailable (nil), so read them leniently.
function Prop($Record, [string]$Name) { $p = $Record.PSObject.Properties[$Name]; if ($p) { $p.Value } else { $null } }
function Probe-Summary($Samples) {
    if ($Samples.Count -lt 2) { return @{ samples = $Samples.Count } }
    $first = $Samples[0]; $last = $Samples[-1]
    $av = [double[]]@($Samples | Where-Object { $null -ne (Prop $_ 'avsync') } | ForEach-Object { [Math]::Abs([double](Prop $_ 'avsync')) * 1000 })
    $delta = { param($name) $l = Prop $last $name; $f = Prop $first $name; if ($null -ne $l -and $null -ne $f) { $l - $f } else { $null } }
    return @{
        samples = $Samples.Count
        hwdecCurrent = @($Samples | ForEach-Object { Prop $_ 'hwdec-current' } | Select-Object -Unique)
        currentVo = Prop $last 'current-vo'; currentAo = Prop $last 'current-ao'; videoCodec = Prop $last 'video-codec'
        width = Prop $last 'width'; height = Prop $last 'height'
        avsyncAbsMsP95 = Percentile $av 95; avsyncAbsMsMax = $(if ($av.Count) { ($av | Measure-Object -Maximum).Maximum } else { $null })
        frameDrops = & $delta 'frame-drop-count'; decoderFrameDrops = & $delta 'decoder-frame-drop-count'
        voDelayedFrames = & $delta 'vo-delayed-frame-count'; mistimedFrames = & $delta 'mistimed-frame-count'
        estimatedVfFps = Median ([double[]]@($Samples | ForEach-Object { Prop $_ 'estimated-vf-fps' } | Where-Object { $null -ne $_ }))
        displayFps = Prop $last 'display-fps'
    }
}

# Qt Quick render loop frame log lines carry the client's wall-clock time.
function Frame-Summary([string]$LogFile, [DateTime]$From, [DateTime]$To, [double]$RefreshHz, [double]$VideoFps) {
    if (-not (Test-Path -LiteralPath $LogFile)) { return @{ measured = $false; reason = 'no client log' } }
    $pattern = '^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) .*frame rendered'
    $stamps = @{}
    foreach ($line in [IO.File]::ReadLines($LogFile)) {
        if ($line -notmatch $pattern) { continue }
        $at = [DateTime]::ParseExact($Matches[1], 'yyyy-MM-dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
        if ($at -lt $From -or $at -gt $To) { continue }
        $key = if ($line -match '\[window ([0-9a-fA-Fx]+)\]') { $Matches[1] } else { 'window' }
        if (-not $stamps.ContainsKey($key)) { $stamps[$key] = New-Object 'System.Collections.Generic.List[DateTime]' }
        $stamps[$key].Add($at)
    }
    if ($stamps.Count -eq 0) { return @{ measured = $false; reason = 'no render loop frame lines in client log' } }
    $main = $stamps.GetEnumerator() | Sort-Object { $_.Value.Count } -Descending | Select-Object -First 1
    $times = $main.Value
    $intervals = New-Object 'System.Collections.Generic.List[double]'
    for ($i = 1; $i -lt $times.Count; ++$i) { $intervals.Add(($times[$i] - $times[$i - 1]).TotalMilliseconds) }
    $values = [double[]]$intervals.ToArray()
    $period = 1000.0 / $RefreshHz; $videoPeriod = 1000.0 / $VideoFps
    $p95 = Percentile $values 95
    return @{
        measured = $true; source = 'qt.scenegraph.time.renderloop'; frames = $times.Count; seconds = ($To - $From).TotalSeconds
        fps = [Math]::Round($times.Count / ($To - $From).TotalSeconds, 2)
        intervalMsP50 = Median $values; intervalMsP95 = $p95; intervalMsP99 = Percentile $values 99
        intervalMsMax = ($values | Measure-Object -Maximum).Maximum
        refreshHz = $RefreshHz; p95InRefreshPeriods = [Math]::Round($p95 / $period, 3)
        p95InVideoFramePeriods = [Math]::Round($p95 / $videoPeriod, 3)
        shareOver125RefreshPeriods = [Math]::Round(@($values | Where-Object { $_ -gt 1.25 * $period }).Count / [Math]::Max(1, $values.Count), 4)
        windowsInLog = $stamps.Count
    }
}

function Start-PresentMon([string]$RunDir) {
    # ETW presents need admin or membership in Performance Log Users.
    if (-not $PresentMonPath) { return $null }
    $csv = Join-Path $RunDir 'presentmon.csv'
    return Start-Process -FilePath $PresentMonPath -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $RunDir 'presentmon.stdout.txt') -RedirectStandardError (Join-Path $RunDir 'presentmon.stderr.txt') -ArgumentList @(
        '--process_id', $script:app.Id, '--output_file', ('"' + $csv + '"'), '--v1_metrics', '--no_console_stats', '--stop_existing_session', '--session_name', 'jfd-prototype')
}
function Stop-PresentMon($Process, [string]$RunDir, [double]$RefreshHz) {
    if (-not $Process) { return @{ measured = $false; reason = 'PresentMon path not given' } }
    & $PresentMonPath --terminate_existing_session --session_name jfd-prototype | Out-Null
    $null = $Process.WaitForExit(10000)
    $csv = Join-Path $RunDir 'presentmon.csv'
    if (-not (Test-Path -LiteralPath $csv)) { return @{ measured = $false; reason = 'no PresentMon output (no presents captured; see presentmon.std*.txt)' } }
    $rows = Import-Csv -LiteralPath $csv
    $column = @('MsBetweenPresents','msBetweenPresents','MsBetweenAppStart') | Where-Object { $rows -and $rows[0].PSObject.Properties[$_] } | Select-Object -First 1
    if (-not $column) { return @{ measured = $false; reason = 'unknown PresentMon columns' } }
    $values = [double[]]@($rows | ForEach-Object { [double]$_.$column } | Where-Object { $_ -gt 0 })
    $p95 = Percentile $values 95
    return @{ measured = $true; source = "PresentMon $column"; presents = $values.Count; intervalMsP50 = Median $values; intervalMsP95 = $p95
              intervalMsP99 = Percentile $values 99; p95InRefreshPeriods = [Math]::Round($p95 / (1000.0 / $RefreshHz), 3) }
}

# ---------------------------------------------------------------- scenarios
function New-RunDir([string]$Name) {
    $dir = Join-Path $out ("{0}-{1:yyyyMMdd-HHmmss}" -f $Name, (Get-Date))
    New-Item -ItemType Directory -Path $dir | Out-Null
    return $dir
}
$primary = $null

function Run-Frames([string]$Res, [string]$Hwdec, [string]$File) {
    $dir = New-RunDir "frames-$Res-$Hwdec-$([IO.Path]::GetFileNameWithoutExtension($File))"
    $pm = $null
    try {
        $load = Start-Client $Hwdec $true $dir 'protocol' $File
        Start-Sleep -Seconds 10
        $pm = Start-PresentMon $dir
        $from = Mark 'window-start'; Start-Sleep -Seconds $FrameWindowSeconds; $to = Mark 'window-end'
        $presentMon = Stop-PresentMon $pm $dir $primary.refreshHz; $pm = $null
        $rect = [JfdWin]::Rect($script:window)
    } finally { if ($pm) { Stop-PresentMon $pm $dir 60 | Out-Null }; $close = Stop-Client }
    $probe = Read-Probe (Join-Path $dir 'mpv-probe.jsonl')
    $mpv = Probe-Summary (Probe-Window $probe 'window-start' 'window-end')
    $frames = Frame-Summary (Join-Path $dir 'client.log') $from $to $primary.refreshHz 60
    return @{ scenario = "frames-$Res"; file = [IO.Path]::GetFileName($File); hwdecSetting = $Hwdec; overlay = $true; load = $load; windowRect = $rect
              frames = $frames; presentMon = $presentMon; mpv = $mpv; close = $close; runDir = $dir }
}

function Run-Cpu([string]$Res, [string]$Hwdec, [string]$File) {
    $dir = New-RunDir "cpu-$Res-$Hwdec-$([IO.Path]::GetFileNameWithoutExtension($File))"
    try {
        $load = Start-Client $Hwdec $false $dir 'none' $File
        Start-Sleep -Seconds 10
        $tree = Get-Tree $script:app.Id
        $null = Mark 'window-start'; $a = Sample-Tree $tree
        Start-Sleep -Seconds $CpuWindowSeconds
        $b = Sample-Tree $tree; $null = Mark 'window-end'
    } finally { $close = Stop-Client }
    $seconds = ($b.at - $a.at).TotalSeconds
    $oneCore = 100.0 * ($b.cpuSeconds - $a.cpuSeconds) / $seconds
    $mpv = Probe-Summary (Probe-Window (Read-Probe (Join-Path $dir 'mpv-probe.jsonl')) 'window-start' 'window-end')
    return @{ scenario = "cpu-$Res"; file = [IO.Path]::GetFileName($File); hwdecSetting = $Hwdec; overlay = $false; load = $load; processes = $b.processes
              cpuPercentOfOneCore = [Math]::Round($oneCore, 1); cpuPercentOfAllCores = [Math]::Round($oneCore / [Environment]::ProcessorCount, 2)
              privateBytes = $b.privateBytes; rssBytes = $b.rssBytes; mpv = $mpv; close = $close; runDir = $dir }
}

function Run-FirstFrameSeek {
    $dir = New-RunDir 'first-frame-seek'
    $loads = @()
    try {
        $null = Start-Client 'auto-safe' $false $dir 'none' $mediaFiles['1080']
        Start-Sleep -Seconds 3
        # The app's own start is the first of the 5 loads, like the fork's first Load-Media.
        for ($i = 1; $i -lt 5; ++$i) { $loads += Load-Media $mediaFiles['1080']; Start-Sleep -Seconds 3 }
        for ($i = 0; $i -lt $Seeks; ++$i) {
            $null = Invoke-Mpv @('seek', $(if ($i % 2) { '20' } else { '50' }), 'absolute-percent'); Start-Sleep -Seconds 3
        }
    } finally { $close = Stop-Client }
    $events = @(Read-Probe (Join-Path $dir 'mpv-probe.jsonl') | Where-Object { $_.kind -eq 'event' })
    $loadMs = @(); $seekMs = @(); $pendingStart = $null; $pendingSeek = $null
    foreach ($e in $events) {
        switch ($e.event) {
            'start-file' { $pendingStart = $e.t; $pendingSeek = $null }
            'seek' { if (-not $pendingStart) { $pendingSeek = $e.t } }
            'playback-restart' {
                if ($pendingStart) { $loadMs += 1000 * ($e.t - $pendingStart); $pendingStart = $null }
                elseif ($pendingSeek) { $seekMs += 1000 * ($e.t - $pendingSeek); $pendingSeek = $null }
            }
        }
    }
    $seekMs = @($seekMs | Select-Object -Last $Seeks)
    return @{ scenario = 'first-frame-seek'; hwdecSetting = 'auto-safe'; loadToFirstFrameMs = $loadMs; seekToFirstFrameMs = $seekMs
              loadMedianMs = Median ([double[]]$loadMs); loadMaxMs = ($loadMs | Measure-Object -Maximum).Maximum
              seekMedianMs = Median ([double[]]$seekMs); seekMaxMs = ($seekMs | Measure-Object -Maximum).Maximum
              appReadyMs = @($loads | ForEach-Object { $_.readyMs }); close = $close; runDir = $dir }
}

function Run-Fullscreen {
    $dir = New-RunDir 'fullscreen-50'
    $results = @()
    try {
        $null = Start-Client 'auto-safe' $false $dir 'none' $mediaFiles['1080']
        Start-Sleep -Seconds 3
        foreach ($screen in $inventory.screens) {
            # Ordinary (non-maximized) window inside this monitor, so drift is visible.
            $null = [JfdWin]::ShowWindow($script:window, 9)
            $b = $screen.bounds
            $null = [JfdWin]::SetWindowPos($script:window, [IntPtr]::Zero, $b[0] + [int]($b[2] * 0.15), $b[1] + [int]($b[3] * 0.15),
                [int]($b[2] * 0.6), [int]($b[3] * 0.6), 0x0004)
            Start-Sleep -Seconds 1
            $initial = [JfdWin]::Rect($script:window)
            $mismatches = 0; $notFullscreen = 0; $maxDrift = 0; $before = $initial
            for ($i = 0; $i -lt 50; ++$i) {
                $null = Invoke-Mpv @('script-message', 'jfd-fullscreen')
                Start-Sleep -Seconds 1
                $full = [JfdWin]::Rect($script:window)
                if ($full[0] -ne $b[0] -or $full[1] -ne $b[1] -or ($full[2] - $full[0]) -ne $b[2] -or ($full[3] - $full[1]) -ne $b[3]) { ++$notFullscreen }
                $null = Invoke-Mpv @('script-message', 'jfd-fullscreen')
                Start-Sleep -Seconds 1
                $after = [JfdWin]::Rect($script:window)
                $drift = (0..3 | ForEach-Object { [Math]::Abs($after[$_] - $initial[$_]) } | Measure-Object -Maximum).Maximum
                if ($drift -gt 1) { ++$mismatches }
                $maxDrift = [Math]::Max($maxDrift, $drift); $before = $after
            }
            $results += @{ monitor = $screen.device; refreshHz = $screen.refreshHz; initialRect = $initial; finalRect = $before
                           togglePairs = 50; pairsWithDrift = $mismatches; maxDriftPx = $maxDrift; fullscreenNotCoveringMonitor = $notFullscreen }
        }
    } finally { $close = Stop-Client }
    return @{ scenario = 'fullscreen-50'; monitors = $results; close = $close; runDir = $dir }
}

function Run-Shutdown {
    $runs = @()
    for ($i = 0; $i -lt 5; ++$i) {
        $dir = New-RunDir "shutdown-$i"
        try { $null = Start-Client 'auto-safe' $false $dir 'protocol' $mediaFiles['1080']; Start-Sleep -Seconds 5 }
        finally { $runs += Stop-Client 15000 }
    }
    $tree = [double[]]@($runs | ForEach-Object { $_.treeExitMs })
    return @{ scenario = 'shutdown'; method = 'WM_CLOSE during 1080p60 playback with overlay'; runs = $runs
              treeExitMedianMs = Median $tree; treeExitMaxMs = ($tree | Measure-Object -Maximum).Maximum
              allExitCodesZero = -not @($runs | Where-Object { $_.exitCode -ne 0 }) }
}

function Run-Memory {
    $dir = New-RunDir 'memory-hour'
    $samples = New-Object 'System.Collections.Generic.List[object]'
    try {
        $null = Start-Client 'auto-safe' $false $dir 'protocol' $mediaFiles['1080']
        $clock = [Diagnostics.Stopwatch]::StartNew(); $nextReload = 60; $nextSample = 0; $tree = Get-Tree $script:app.Id; $nextTree = 30
        $total = $MemoryWarmupSeconds + $MemoryMeasureSeconds
        while ($clock.Elapsed.TotalSeconds -lt $total) {
            if ($script:app.HasExited) { throw 'Client exited during memory run' }
            $s = $clock.Elapsed.TotalSeconds
            if ($s -ge $nextTree) { $tree = Get-Tree $script:app.Id; $nextTree += 30 }
            if ($s -ge $nextSample) {
                $sample = Sample-Tree $tree; $sample.elapsed = [Math]::Round($s, 1); $samples.Add($sample); $nextSample += 5
                if ($samples.Count % 60 -eq 0) { Log ("memory {0:N0} s: private {1:N0} MiB" -f $s, ($sample.privateBytes / 1MB)) }
            }
            if ($s -ge $nextReload) { $null = Load-Media $mediaFiles['1080']; $nextReload += 60 }
            Start-Sleep -Milliseconds 250
        }
    } finally { $close = Stop-Client }
    $measured = @($samples | Where-Object { $_.elapsed -ge $MemoryWarmupSeconds })
    $firstWindow = @($measured | Where-Object { $_.elapsed -lt $MemoryWarmupSeconds + 300 })
    $lastWindow = @($measured | Where-Object { $_.elapsed -ge $MemoryWarmupSeconds + $MemoryMeasureSeconds - 300 })
    $growth = { param($name) (Median ([double[]]@($lastWindow | ForEach-Object { $_.$name }))) - (Median ([double[]]@($firstWindow | ForEach-Object { $_.$name }))) }
    Write-Json @($samples | ForEach-Object { @{ s = $_.elapsed; privateBytes = $_.privateBytes; rssBytes = $_.rssBytes; processes = $_.processes } }) (Join-Path $dir 'samples.json')
    return @{ scenario = 'memory-hour'; warmupSeconds = $MemoryWarmupSeconds; measureSeconds = $MemoryMeasureSeconds; reloadEverySeconds = 60
              samples = $samples.Count; privateGrowthBytes = & $growth 'privateBytes'; rssGrowthBytes = & $growth 'rssBytes'
              privatePeakBytes = ($measured | ForEach-Object { $_.privateBytes } | Measure-Object -Maximum).Maximum
              rssPeakBytes = ($measured | ForEach-Object { $_.rssBytes } | Measure-Object -Maximum).Maximum
              close = $close; runDir = $dir }
}

# Screen recording for the pull request, after the measurement windows: the
# app's own menu over 1080p60, opened and closed, then fullscreen and back.
function Run-Recording {
    $dir = New-RunDir 'recording'
    $video = Join-Path $dir 'recording.mp4'
    $ffmpegProcess = $null
    try {
        $null = Start-Client 'auto-safe' $false $dir 'menu' $mediaFiles['1080'] @('--animate-menu', '--stats')
        Start-Sleep -Seconds 2
        $ffmpegProcess = Start-Process -FilePath $Ffmpeg -PassThru -WindowStyle Hidden `
            -RedirectStandardError (Join-Path $dir 'ffmpeg.txt') -ArgumentList @('-hide_banner', '-y', '-f', 'gdigrab', '-framerate', '60',
            '-t', "$RecordingSeconds", '-i', 'desktop', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '23', '-pix_fmt', 'yuv420p', ('"' + $video + '"'))
        $third = [Math]::Max(3, [int]($RecordingSeconds / 3))
        Start-Sleep -Seconds $third
        $null = Invoke-Mpv @('script-message', 'jfd-overlay', 'protocol'); Start-Sleep -Seconds $third
        $null = Invoke-Mpv @('script-message', 'jfd-overlay', 'menu')
        $null = Invoke-Mpv @('script-message', 'jfd-fullscreen'); Start-Sleep -Seconds ([Math]::Max(1, $third - 2))
        $null = Invoke-Mpv @('script-message', 'jfd-fullscreen')
        $null = $ffmpegProcess.WaitForExit(($RecordingSeconds + 30) * 1000)
    } finally {
        if ($ffmpegProcess -and -not $ffmpegProcess.HasExited) { Stop-Process -Id $ffmpegProcess.Id -Force -ErrorAction SilentlyContinue }
        $close = Stop-Client
    }
    return @{ scenario = 'recording'; file = $(if (Test-Path -LiteralPath $video) { $video } else { $null }); seconds = $RecordingSeconds; close = $close; runDir = $dir }
}

# ---------------------------------------------------------------- main
# Keep the display on and the PC awake while this script runs, like a video
# player does; released automatically when the script exits.
$null = [JfdWin]::SetThreadExecutionState([uint32]'0x80000003')
$inventory = Get-Inventory
$primary = $inventory.screens | Where-Object { $_.primary } | Select-Object -First 1
$report = [ordered]@{
    schemaVersion = 1; protocol = 'architecture/measurement-protocol.md rev. 2'; client = 'prototype'
    startedAt = (Get-Date).ToString('o'); harnessSha256 = Sha256 $PSCommandPath; probeSha256 = Sha256 $probeScript
    exeSha256 = Sha256 $appExe
    appBuild = $(if (Test-Path -LiteralPath (Join-Path $appRoot 'build-info.json')) { Get-Content -LiteralPath (Join-Path $appRoot 'build-info.json') -Raw | ConvertFrom-Json } else { $null })
    media = @(($mediaLists['1080'] + $mediaLists['2160']) | ForEach-Object { @{ file = $_; sha256 = Sha256 $_ } })
    inventory = $inventory; results = @(); failures = @()
}
if ($inventory.remoteSession) { Log 'WARNING: remote desktop session; GPU, frame and CPU numbers are not representative' }
$reportPath = Join-Path $out 'prototype-report.json'
Write-Json $report $reportPath
$jobs = foreach ($scenario in $Scenarios) {
    if ($scenario -match '^(frames|cpu)-(1080|2160)$') {
        $kind = $Matches[1]; $res = $Matches[2]
        foreach ($f in $mediaLists[$res]) { foreach ($h in $HardwareDecoding) {
            @{ name = "$scenario-$h-$([IO.Path]::GetFileName($f))"; kind = $kind; res = $res; hwdec = $h; file = $f } } }
    } elseif ($scenario -in @('first-frame-seek','fullscreen-50','shutdown','memory-hour','recording')) {
        @{ name = $scenario; kind = $scenario }
    } else { throw "Unknown scenario $scenario" }
}
foreach ($job in $jobs) {
    Log "Running $($job.name)"
    try {
        $result = switch ($job.kind) {
            'frames' { Run-Frames $job.res $job.hwdec $job.file }
            'cpu' { Run-Cpu $job.res $job.hwdec $job.file }
            'first-frame-seek' { Run-FirstFrameSeek }
            'fullscreen-50' { Run-Fullscreen }
            'shutdown' { Run-Shutdown }
            'memory-hour' { Run-Memory }
            'recording' { Run-Recording }
        }
        $report.results += $result
        Log "Finished $($job.name)"
    } catch {
        Log "FAILED $($job.name) : $($_.Exception.Message) $($_.InvocationInfo.PositionMessage)"
        $report.failures += @{ scenario = $job.name; error = $_.Exception.Message; at = (Get-Date).ToString('o') }
        try { Stop-Client | Out-Null } catch {}
    }
    Write-Json $report $reportPath
}
$report.finishedAt = (Get-Date).ToString('o')
Write-Json $report $reportPath
Log "Report: $reportPath"
