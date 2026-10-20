# Builds the per-user MSI from a deployed application folder (windeployqt
# output). WiX comes from NuGet as a .NET tool at the version pinned in
# deps/versions.json (windows.wix).
#
#   packaging/windows/build-msi.ps1 -Deployment build/package/app -Version 0.0.1 -Output build/jellyfin-desktop-next.msi
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Deployment,
    [Parameter(Mandatory)] [string]$Version,
    [Parameter(Mandatory)] [string]$Output,
    [string]$ExeName = 'jellyfin-desktop-next.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$wix = (Get-Content (Join-Path $root 'deps/versions.json') -Raw | ConvertFrom-Json).windows.wix
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "MSI versions have three numbers, got '$Version'" }
$Deployment = (Resolve-Path $Deployment).Path
if (-not (Test-Path -LiteralPath (Join-Path $Deployment $ExeName))) { throw "$ExeName is not in $Deployment" }

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

# A tool path of our own, so a WiX installed on the machine is never used.
$tools = Join-Path $root 'build/tools/wix'
$wixExe = Join-Path $tools 'wix.exe'
$installed = if (Test-Path -LiteralPath $wixExe) { (& $wixExe --version) -replace '\+.*$', '' } else { '' }
if ($installed -ne $wix.version) {
    if (Test-Path -LiteralPath $tools) { Remove-Item -LiteralPath $tools -Recurse -Force }
    Invoke-Checked 'dotnet' @('tool', 'install', 'wix', '--version', $wix.version, '--tool-path', $tools)
}
foreach ($extension in $wix.extensions) {
    Invoke-Checked $wixExe @('extension', 'add', '-g', "$extension/$($wix.version)", '-acceptEula', $wix.eulaId)
}

$outputPath = [IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force -Path (Split-Path $outputPath) | Out-Null
$arguments = @('build', (Join-Path $PSScriptRoot 'jellyfin-desktop-next.wxs'),
    '-acceptEula', $wix.eulaId, '-arch', 'x64',
    '-bindpath', "Deployment=$Deployment",
    '-d', "Version=$Version", '-d', "ExeName=$ExeName",
    '-d', "IconFile=$(Join-Path $root 'packaging/icons/jellyfin-desktop-next.ico')",
    # Per-user files under LocalAppData have file key paths; the uninstall
    # removes the folder itself (util:RemoveFolderEx), so these ICEs about
    # per-user components do not apply.
    '-sice', 'ICE38', '-sice', 'ICE64', '-sice', 'ICE91',
    '-o', $outputPath)
foreach ($extension in $wix.extensions) { $arguments += @('-ext', $extension) }
Invoke-Checked $wixExe $arguments
Write-Host "MSI: $outputPath ($([Math]::Round((Get-Item $outputPath).Length / 1MB, 1)) MiB)"
