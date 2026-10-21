# Installs the MSI for the current user without a UI, checks the files,
# shortcuts and registry key, runs the installed application's smoke test,
# then uninstalls (with REMOVEPROFILE=1) and checks that nothing is left.
#
#   packaging/windows/test-msi.ps1 -Msi build/jellyfin-desktop-next.msi -Logs build/msi-logs
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Msi,
    [Parameter(Mandatory)] [string]$Logs,
    [string]$ExeName = 'jellyfin-desktop-next.exe',
    [string]$ProductName = 'Jellyfin Desktop Next'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Msi = (Resolve-Path $Msi).Path
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$Logs = (Resolve-Path $Logs).Path
$installFolder = Join-Path $env:LOCALAPPDATA "Programs\$ProductName"
$exe = Join-Path $installFolder $ExeName
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) "$ProductName.lnk"
$desktop = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) "$ProductName.lnk"
$registryKey = "HKCU:\Software\$ProductName"
$localProfile = Join-Path $env:LOCALAPPDATA $ProductName
$roamingProfile = Join-Path $env:APPDATA $ProductName

function Invoke-Msiexec([string[]]$Arguments, [string]$Log) {
    $process = Start-Process -FilePath 'msiexec.exe' -Wait -PassThru -ArgumentList ($Arguments + @('/qn', '/l*v', "`"$Log`""))
    if ($process.ExitCode -ne 0) {
        Get-Content $Log | Select-String -Pattern 'error|failed|Return value 3' | Select-Object -Last 30 | ForEach-Object Line
        throw "msiexec $($Arguments[0]) failed with exit code $($process.ExitCode)"
    }
}

function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    Write-Host "ok: $Message"
}

# No administrator rights: the package says it needs no elevation (word count
# bit 8) and does not set ALLUSERS.
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @($Msi, 0))
$summary = $database.GetType().InvokeMember('SummaryInformation', 'GetProperty', $null, $database, @(0))
$wordCount = [int]$summary.GetType().InvokeMember('Property', 'GetProperty', $null, $summary, @(15))
Assert (($wordCount -band 8) -ne 0) "package needs no elevation (word count $wordCount)"
$view = $database.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $database, @("SELECT ``Value`` FROM ``Property`` WHERE ``Property``='ALLUSERS'"))
$view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null) | Out-Null
$record = $view.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $view, $null)
Assert ($null -eq $record) 'ALLUSERS is not set'
$view.GetType().InvokeMember('Close', 'InvokeMethod', $null, $view, $null) | Out-Null
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($database)

Invoke-Msiexec @('/i', "`"$Msi`"") (Join-Path $Logs 'install.log')
Assert (Test-Path -LiteralPath $exe) "installed $exe"
Assert (Test-Path -LiteralPath $startMenu) 'Start menu shortcut'
Assert (Test-Path -LiteralPath $desktop) 'desktop shortcut'
Assert ((Get-ItemProperty -Path $registryKey -Name InstallFolder).InstallFolder.TrimEnd('\') -eq $installFolder) 'install folder remembered'
# Per-user: nothing under Program Files.
Assert (-not (Test-Path -LiteralPath (Join-Path $env:ProgramFiles $ProductName))) 'nothing in Program Files'

# The installed copy must run on its own, with only Windows on PATH.
$savedPath = $env:PATH
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_OPENGL = 'software'
try {
    $out = Join-Path $Logs 'smoke.out'
    $err = Join-Path $Logs 'smoke.err'
    $process = Start-Process -FilePath $exe -PassThru -Wait -ArgumentList @('--smoke') `
        -RedirectStandardOutput $out -RedirectStandardError $err
    Get-Content $out, $err
    Assert ($process.ExitCode -eq 0) 'installed app smoke test'
} finally {
    $env:PATH = $savedPath
}

# Something the application could have written: files the installer does not
# know about must go too.
New-Item -ItemType File -Force -Path (Join-Path $installFolder 'written-later.log') | Out-Null
foreach ($folder in @($localProfile, $roamingProfile)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $folder 'cache') | Out-Null
    Set-Content -Path (Join-Path $folder 'settings.ini') -Value 'x=1'
}

# A second install of the same version upgrades in place.
Invoke-Msiexec @('/i', "`"$Msi`"") (Join-Path $Logs 'reinstall.log')
Assert (Test-Path -LiteralPath $exe) 'same-version reinstall keeps the application'
Assert (Test-Path -LiteralPath (Join-Path $localProfile 'settings.ini')) 'reinstall keeps the profile'

# Default uninstall keeps the profile.
Invoke-Msiexec @('/x', "`"$Msi`"") (Join-Path $Logs 'uninstall.log')
Assert (-not (Test-Path -LiteralPath $installFolder)) 'install folder removed'
Assert (-not (Test-Path -LiteralPath $startMenu)) 'Start menu shortcut removed'
Assert (-not (Test-Path -LiteralPath $desktop)) 'desktop shortcut removed'
Assert (-not (Test-Path -LiteralPath $registryKey)) 'registry key removed'
Assert (Test-Path -LiteralPath (Join-Path $localProfile 'settings.ini')) 'profile kept without REMOVEPROFILE'

# With REMOVEPROFILE=1 the profile goes too.
Invoke-Msiexec @('/i', "`"$Msi`"") (Join-Path $Logs 'install-2.log')
Invoke-Msiexec @('/x', "`"$Msi`"", 'REMOVEPROFILE=1') (Join-Path $Logs 'uninstall-profile.log')
Assert (-not (Test-Path -LiteralPath $installFolder)) 'install folder removed again'
Assert (-not (Test-Path -LiteralPath $localProfile)) 'local profile removed with REMOVEPROFILE=1'
Assert (-not (Test-Path -LiteralPath $roamingProfile)) 'roaming profile removed with REMOVEPROFILE=1'
