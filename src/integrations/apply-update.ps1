param([int]$ProcessId, [string]$Msi, [string]$Exe, [string]$Log, [string]$Marker,
      [ValidateSet('/qb', '/qn')] [string]$UiLevel = '/qb')
# Installs a downloaded MSI over the running program (MsiUpdater::apply).
# Built into the client (MsiUpdater::updateScript) and run as is against real
# MSIs in CI (packaging/windows/test-msi-update.ps1). The client never passes
# UiLevel: people see msiexec's progress and errors (/qb). CI passes /qn for
# the broken-package case, where nobody can close the error box.
$ErrorActionPreference = 'Stop'
# msiexec does not open a package named with forward slashes.
$Msi = [System.IO.Path]::GetFullPath($Msi)
$Log = [System.IO.Path]::GetFullPath($Log)
Remove-Item -LiteralPath "$Marker.failed" -Force -ErrorAction SilentlyContinue
try { Wait-Process -Id $ProcessId -Timeout 120 } catch { }
$code = 1
try {
  $install = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', "`"$Msi`"", $UiLevel, '/norestart', '/l*v', "`"$Log`"") -Wait -PassThru
  $code = $install.ExitCode
} catch { }
if ($code -ne 0 -and $code -ne 3010) { New-Item -ItemType File -Path "$Marker.failed" -Force | Out-Null }
Remove-Item -LiteralPath $Msi -Force -ErrorAction SilentlyContinue
try { Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) } catch { }
