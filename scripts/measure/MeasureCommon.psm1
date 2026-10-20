# Shared helpers for the measurement scripts. Windows PowerShell 5.1 compatible.
Set-StrictMode -Version Latest

function Get-ProcessTreeIds([int]$RootId) {
    # The process and all its descendants (QtWebEngine helpers for the 2.x client).
    $all = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId)
    $ids = New-Object System.Collections.Generic.List[int]
    $ids.Add($RootId)
    for ($i = 0; $i -lt $ids.Count; $i++) {
        foreach ($p in $all) { if ($p.ParentProcessId -eq $ids[$i] -and -not $ids.Contains([int]$p.ProcessId)) { $ids.Add([int]$p.ProcessId) } }
    }
    return $ids
}

function Get-ProcessTreeSample([int]$RootId) {
    # One sample of CPU time and memory of a process tree, as seen from outside.
    $cpu = 0.0; $private = 0; $workingSet = 0; $count = 0
    foreach ($id in (Get-ProcessTreeIds $RootId)) {
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if (-not $p) { continue }
        try { $cpu += $p.TotalProcessorTime.TotalSeconds } catch { }
        $private += $p.PrivateMemorySize64
        $workingSet += $p.WorkingSet64
        $count++
    }
    return [pscustomobject]@{
        t = [Diagnostics.Stopwatch]::GetTimestamp() / [Diagnostics.Stopwatch]::Frequency
        cpuSeconds = $cpu; privateBytes = $private; workingSetBytes = $workingSet; processes = $count
    }
}

function Measure-ProcessTree {
    # Samples a running process tree every IntervalSeconds until it exits or
    # DurationSeconds pass. Returns the samples plus average CPU (percent of one
    # core) and private-bytes growth after WarmupSeconds.
    param([Parameter(Mandatory)] [Diagnostics.Process]$Process,
          [double]$DurationSeconds = 60, [double]$IntervalSeconds = 5, [double]$WarmupSeconds = 0)
    $samples = New-Object System.Collections.Generic.List[object]
    $start = Get-ProcessTreeSample $Process.Id
    $samples.Add($start)
    $deadline = $start.t + $DurationSeconds
    while (-not $Process.HasExited) {
        Start-Sleep -Milliseconds ([int]($IntervalSeconds * 1000))
        if ($Process.HasExited) { break }
        $sample = Get-ProcessTreeSample $Process.Id
        $samples.Add($sample)
        if ($sample.t -ge $deadline) { break }
    }
    $last = $samples[$samples.Count - 1]
    $baseline = $samples | Where-Object { $_.t - $start.t -ge $WarmupSeconds } | Select-Object -First 1
    $wall = $last.t - $start.t
    return [pscustomobject]@{
        seconds = $wall
        cpuPercentOfOneCore = if ($wall -gt 0) { 100.0 * ($last.cpuSeconds - $start.cpuSeconds) / $wall } else { 0 }
        privateGrowthBytes = if ($baseline) { $last.privateBytes - $baseline.privateBytes } else { $null }
        privatePeakBytes = ($samples | Measure-Object privateBytes -Maximum).Maximum
        samples = $samples
    }
}

function Stop-ByClosingWindow {
    # Closes the main window like a user (WM_CLOSE) and times the exit.
    param([Parameter(Mandatory)] [Diagnostics.Process]$Process, [int]$TimeoutSeconds = 20)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $closed = $Process.CloseMainWindow()
    $exited = $Process.WaitForExit($TimeoutSeconds * 1000)
    $watch.Stop()
    if (-not $exited) { Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue }
    return [pscustomobject]@{
        closeMessageSent = $closed
        exited = $exited
        milliseconds = $watch.Elapsed.TotalMilliseconds
        exitCode = if ($exited) { $Process.ExitCode } else { $null }
    }
}

function Wait-MainWindow([Diagnostics.Process]$Process, [int]$TimeoutSeconds = 30) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline -and -not $Process.HasExited) {
        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

Export-ModuleMember -Function Get-ProcessTreeSample, Measure-ProcessTree, Stop-ByClosingWindow, Wait-MainWindow
