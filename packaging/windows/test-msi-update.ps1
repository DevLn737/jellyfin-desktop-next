# Runs the client's update script (src/integrations/apply-update.ps1, the
# text MsiUpdater::updateScript() writes) against real MSIs, the way
# MsiUpdater::apply() starts it:
#   1. install the old MSI;
#   2. run the script with a process to wait for, the new MSI, the installed
#      program and a marker: the new version must be installed (one product,
#      the new ProductVersion), no .failed marker, the MSI removed, the
#      installed program started again;
#   3. run it with a broken MSI: .failed written, the installed version
#      unchanged, the program started again;
#   4. run the old MSI over the new one: refused as a downgrade;
#   5. uninstall.
#
#   packaging/windows/test-msi-update.ps1 -OldMsi old.msi -OldVersion 0.0.999 -NewMsi new.msi -NewVersion 0.0.1000 `
#     -Script src/integrations/apply-update.ps1 -Logs build/msi-update-logs
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$OldMsi,
    [Parameter(Mandatory)] [string]$OldVersion,
    [Parameter(Mandatory)] [string]$NewMsi,
    [Parameter(Mandatory)] [string]$NewVersion,
    [Parameter(Mandatory)] [string]$Script,
    [Parameter(Mandatory)] [string]$Logs,
    [string]$ExeName = 'jellyfin-desktop.exe',
    [string]$ProductName = 'Jellyfin Desktop Next',
    [int]$RelaunchSeconds = 90
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$OldMsi = (Resolve-Path $OldMsi).Path
$NewMsi = (Resolve-Path $NewMsi).Path
$Script = (Resolve-Path $Script).Path
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$Logs = (Resolve-Path $Logs).Path
$work = Join-Path $Logs 'work'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$installFolder = Join-Path $env:LOCALAPPDATA "Programs\$ProductName"
$exe = Join-Path $installFolder $ExeName
$processName = [IO.Path]::GetFileNameWithoutExtension($ExeName)
$wxs = Get-Content (Join-Path $PSScriptRoot 'jellyfin-desktop-next.wxs') -Raw
if ($wxs -notmatch 'define UpgradeCode = "([0-9a-fA-F-]+)"') { throw 'no UpgradeCode in the .wxs' }
$upgradeCode = '{' + $Matches[1].ToUpperInvariant() + '}'

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAILED: $Message" }
    Write-Host "ok: $Message"
}

function Show-Log([string]$Log) {
    if (Test-Path -LiteralPath $Log) {
        Get-Content $Log | Select-String -Pattern 'error|failed|Return value 3|returning' | Select-Object -Last 30 | ForEach-Object Line
    }
}

function Invoke-Msiexec([string[]]$Arguments, [string]$Log) {
    $process = Start-Process -FilePath 'msiexec.exe' -Wait -PassThru -ArgumentList ($Arguments + @('/qn', '/l*v', "`"$Log`""))
    return $process.ExitCode
}

# Windows Installer's own record: the ProductVersion of every installed
# product with this UpgradeCode.
function Get-InstalledVersions {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $type = $installer.GetType()
    $products = $type.InvokeMember('RelatedProducts', 'GetProperty', $null, $installer, @($upgradeCode))
    $count = [int]$products.GetType().InvokeMember('Count', 'GetProperty', $null, $products, $null)
    $versions = @()
    for ($i = 0; $i -lt $count; $i++) {
        $code = $products.GetType().InvokeMember('Item', 'GetProperty', $null, $products, @($i))
        $versions += [string]$type.InvokeMember('ProductInfo', 'GetProperty', $null, $installer, @($code, 'VersionString'))
    }
    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($installer)
    # Unrolled: callers collect it with @(), which gives an array for 0, 1 or more.
    return $versions
}

function Get-App {
    @(Get-Process -Name $processName -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -eq $exe } catch { $false }
    })
}

function Stop-App {
    Get-Process -Name $processName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Process -Name $processName -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
}

# The update script started the installed program: it is running from the
# install folder (checked often: it is stopped here as soon as it is seen).
function Wait-Relaunch([string]$What) {
    $deadline = (Get-Date).AddSeconds($RelaunchSeconds)
    $seen = @()
    while ((Get-Date) -lt $deadline) {
        $seen = @(Get-App)
        if ($seen.Count -gt 0) { break }
        Start-Sleep -Milliseconds 100
    }
    Assert ($seen.Count -gt 0) "$What started $exe again"
    Stop-App
}

# Starts the update script as MsiUpdater::apply() does (Windows PowerShell
# 5.1, the same switches as MsiUpdater::scriptArguments), with a process of
# its own to wait for, and returns the waited-for process and the script's
# exit code.
function Invoke-UpdateScript([string]$Msi, [string]$Log, [string]$Marker, [string[]]$Extra = @()) {
    $waited = Start-Process -FilePath 'powershell.exe' -PassThru -WindowStyle Hidden `
        -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 5')
    $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden',
        '-File', "`"$Script`"", '-ProcessId', $waited.Id, '-Msi', "`"$Msi`"", '-Exe', "`"$exe`"",
        '-Log', "`"$Log`"", '-Marker', "`"$Marker`"") + $Extra
    $process = Start-Process -FilePath 'powershell.exe' -PassThru -ArgumentList $arguments
    $null = $process.Handle, $waited.Handle # keeps the exit code and time readable after the processes end
    if (-not $process.WaitForExit(600000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Show-Log $Log
        throw 'the update script did not finish within 10 minutes'
    }
    $waited.WaitForExit()
    return @{ Waited = $waited; ExitCode = $process.ExitCode }
}

Stop-App
Assert (@(Get-InstalledVersions).Count -eq 0) 'nothing installed before the test'

# 1. The old version.
$code = Invoke-Msiexec @('/i', "`"$OldMsi`"") (Join-Path $Logs 'update-install-old.log')
if ($code -ne 0) { Show-Log (Join-Path $Logs 'update-install-old.log') }
Assert ($code -eq 0) "old MSI ($OldVersion) installed"
$installed = @(Get-InstalledVersions)
Assert (($installed.Count -eq 1) -and ($installed[0] -eq $OldVersion)) "installed version is $OldVersion (got $($installed -join ', '))"
Assert (Test-Path -LiteralPath $exe) "installed $exe"

try {
    # 2. Update to the new version. The client downloads the MSI into its
    # state folder; the script deletes it after the install.
    $msi = Join-Path $work "jellyfin-desktop-next-$NewVersion-x64.msi"
    Copy-Item -LiteralPath $NewMsi -Destination $msi -Force
    $marker = Join-Path $work 'update-pending.json'
    Set-Content -LiteralPath $marker -Value '{"version":"new","route":""}' -Encoding ASCII
    $log = Join-Path $Logs 'update-script-install.log'
    $run = Invoke-UpdateScript $msi $log $marker
    Assert ($run.ExitCode -eq 0) "update script exit code 0 (got $($run.ExitCode))"
    if (-not (Test-Path -LiteralPath $log)) { throw 'the update script did not run msiexec (no log)' }
    # It waited for the program to exit before installing.
    Assert ((Get-Item -LiteralPath $log).CreationTime -ge $run.Waited.ExitTime.AddSeconds(-1)) 'msiexec started after the waited-for process exited'
    $installed = @(Get-InstalledVersions)
    if (-not (($installed.Count -eq 1) -and ($installed[0] -eq $NewVersion))) { Show-Log $log }
    Assert (($installed.Count -eq 1) -and ($installed[0] -eq $NewVersion)) "installed version is $NewVersion, the old one is gone (got $($installed -join ', '))"
    Assert (-not (Test-Path -LiteralPath "$marker.failed")) 'no .failed marker after a good install'
    Assert (Test-Path -LiteralPath $marker) 'the marker stays for the new version to read'
    Assert (-not (Test-Path -LiteralPath $msi)) 'the installed MSI was removed'
    Wait-Relaunch 'a successful update'

    # 3. A package msiexec cannot open. The client shows msiexec's errors
    # (/qb); here nobody can answer a message box, so this run asks for no
    # UI. Everything else is the same script.
    $broken = Join-Path $work "jellyfin-desktop-next-broken-x64.msi"
    [IO.File]::WriteAllBytes($broken, [byte[]](1..4096 | ForEach-Object { $_ % 251 }))
    $log = Join-Path $Logs 'update-script-broken.log'
    $run = Invoke-UpdateScript $broken $log $marker @('-UiLevel', '/qn')
    Assert ($run.ExitCode -eq 0) "update script exit code 0 with a broken MSI (got $($run.ExitCode))"
    Assert (Test-Path -LiteralPath "$marker.failed") 'a failed install writes the .failed marker'
    Assert (-not (Test-Path -LiteralPath $broken)) 'the broken MSI was removed'
    $installed = @(Get-InstalledVersions)
    Assert (($installed.Count -eq 1) -and ($installed[0] -eq $NewVersion)) "a failed install keeps $NewVersion (got $($installed -join ', '))"
    Wait-Relaunch 'a failed update'

    # 4. The older package over the newer one is a downgrade.
    $log = Join-Path $Logs 'update-downgrade.log'
    $code = Invoke-Msiexec @('/i', "`"$OldMsi`"") $log
    Assert ($code -ne 0) "the old MSI is refused over the new one (exit code $code)"
    if (Select-String -LiteralPath $log -Pattern 'A newer version of' -Quiet) { Write-Host 'ok: refused with the downgrade message' }
    else { Show-Log $log }
    $installed = @(Get-InstalledVersions)
    Assert (($installed.Count -eq 1) -and ($installed[0] -eq $NewVersion)) "a refused downgrade keeps $NewVersion (got $($installed -join ', '))"
} finally {
    Stop-App
    # 5. Leave the runner clean.
    $code = Invoke-Msiexec @('/x', "`"$NewMsi`"", 'REMOVEPROFILE=1') (Join-Path $Logs 'update-uninstall.log')
    if (@(Get-InstalledVersions).Count -gt 0) {
        $null = Invoke-Msiexec @('/x', "`"$OldMsi`"", 'REMOVEPROFILE=1') (Join-Path $Logs 'update-uninstall-old.log')
    }
}
Assert (@(Get-InstalledVersions).Count -eq 0) 'uninstalled'
