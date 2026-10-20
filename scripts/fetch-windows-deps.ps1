# Downloads the pinned Windows Qt, libmpv and Vulkan loader from
# deps/versions.json, verifies their size and SHA256, and creates an MSVC import
# library for libmpv. Run from a Visual Studio developer shell (dumpbin and lib
# are needed). In GitHub Actions it also exports QT_ROOT_DIR, MPV_INCLUDE_DIR,
# MPV_LIBRARY and JFD_RUNTIME_DIR, and puts the Qt and runtime directories on PATH.
[CmdletBinding()]
param(
    [string]$Destination = (Join-Path $PSScriptRoot '../build/deps')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$pins = Get-Content (Join-Path $root 'deps/versions.json') -Raw | ConvertFrom-Json
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

function Get-Pinned($Pin) {
    $file = Join-Path $Destination $Pin.name
    if (-not (Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $Pin.sha256) {
        for ($attempt = 1; ; $attempt++) {
            try {
                Invoke-WebRequest -Uri $Pin.url -OutFile $file -UseBasicParsing
                break
            } catch {
                if ($attempt -ge 4) { throw }
                Start-Sleep -Seconds ([Math]::Pow(2, $attempt))
            }
        }
    }
    $length = (Get-Item -LiteralPath $file).Length
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($length -ne $Pin.bytes -or $hash -ne $Pin.sha256) {
        throw "$($Pin.name): got $length bytes, SHA256 $hash; pinned $($Pin.bytes) bytes, $($Pin.sha256)"
    }
    return $file
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $output = & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
    return $output
}

# Qt: official archives unpacked into one prefix. Already-unpacked archives are
# remembered by hash, so a cached prefix is reused.
$qtRoot = Join-Path $Destination "qt-$($pins.qt.version)"
New-Item -ItemType Directory -Force -Path $qtRoot | Out-Null
foreach ($archive in $pins.windows.qt.archives) {
    $marker = Join-Path $qtRoot ".$($archive.sha256)"
    if (Test-Path -LiteralPath $marker) { continue }
    $file = Get-Pinned $archive
    Invoke-Checked '7z' @('x', '-y', "-o$qtRoot", $file) | Out-Null
    New-Item -ItemType File -Force -Path $marker | Out-Null
}
$qtCore = Get-ChildItem -LiteralPath $qtRoot -Recurse -Filter 'Qt6Core.dll' | Select-Object -First 1
if (-not $qtCore) { throw 'Qt6Core.dll not found in the unpacked Qt archives' }
$qtPrefix = Split-Path (Split-Path $qtCore.FullName)
# The OpenGL software renderer and shader compiler archives hold bare DLLs.
foreach ($dll in @('opengl32sw.dll', 'd3dcompiler_47.dll')) {
    $found = Get-ChildItem -LiteralPath $qtRoot -Recurse -Filter $dll | Where-Object { $_.DirectoryName -ne (Join-Path $qtPrefix 'bin') } | Select-Object -First 1
    if ($found) { Copy-Item -LiteralPath $found.FullName -Destination (Join-Path $qtPrefix 'bin') -Force }
}
[IO.File]::WriteAllText((Join-Path $qtPrefix 'bin/qt.conf'), "[Paths]`nPrefix=..`n")
if (-not (Test-Path -LiteralPath (Join-Path $qtPrefix 'lib/cmake/Qt6Quick/Qt6QuickConfig.cmake'))) {
    throw 'The unpacked Qt has no Qt Quick CMake package'
}

# libmpv (shinchiro build): headers, libmpv-2.dll.
$mpvRoot = Join-Path $Destination 'mpv'
$mpvArchive = Get-Pinned $pins.windows.mpv
if (Test-Path -LiteralPath $mpvRoot) { Remove-Item -LiteralPath $mpvRoot -Recurse -Force }
Invoke-Checked '7z' @('x', '-y', "-o$mpvRoot", $mpvArchive) | Out-Null
$mpvDll = Join-Path $mpvRoot 'libmpv-2.dll'
if (-not (Test-Path -LiteralPath (Join-Path $mpvRoot 'include/mpv/render_gl.h')) -or -not (Test-Path -LiteralPath $mpvDll)) {
    throw 'The libmpv archive does not have the expected layout'
}

# MSVC cannot link the MinGW import library; build one from the DLL exports.
$exports = Invoke-Checked 'dumpbin.exe' @('/nologo', '/exports', $mpvDll) |
    ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(mpv_[A-Za-z0-9_]+)\s*$') { $Matches[1] } }
if (@($exports).Count -lt 20) { throw 'Unexpected libmpv export table' }
$definition = Join-Path $mpvRoot 'libmpv-2.def'
[IO.File]::WriteAllText($definition, "LIBRARY libmpv-2.dll`r`nEXPORTS`r`n" + ($exports -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
$mpvLibrary = Join-Path $mpvRoot 'libmpv-2.lib'
Invoke-Checked 'lib.exe' @('/nologo', "/def:$definition", "/out:$mpvLibrary", '/MACHINE:X64') | Out-Null

# libmpv-2.dll imports vulkan-1.dll, which a clean Windows does not have.
$vulkanRoot = Join-Path $Destination 'vulkan'
$vulkanArchive = Get-Pinned $pins.windows.vulkanRuntime
if (Test-Path -LiteralPath $vulkanRoot) { Remove-Item -LiteralPath $vulkanRoot -Recurse -Force }
Expand-Archive -LiteralPath $vulkanArchive -DestinationPath $vulkanRoot
$vulkanDll = Join-Path $vulkanRoot $pins.windows.vulkanRuntime.dll
if ((Get-FileHash -LiteralPath $vulkanDll -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pins.windows.vulkanRuntime.dllSha256) {
    throw 'vulkan-1.dll differs from its pin'
}

$runtime = Join-Path $Destination 'runtime'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
Copy-Item -LiteralPath $mpvDll, $vulkanDll -Destination $runtime -Force

$include = Join-Path $mpvRoot 'include'
Write-Host "Qt $($pins.qt.version): $qtPrefix"
Write-Host "libmpv $($pins.windows.mpv.version): $mpvLibrary"
Write-Host "runtime: $runtime"
if ($env:GITHUB_ENV) {
    "QT_ROOT_DIR=$qtPrefix" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
    "CMAKE_PREFIX_PATH=$qtPrefix" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
    (Join-Path $qtPrefix 'bin') | Out-File -Append -Encoding utf8 $env:GITHUB_PATH
    "MPV_INCLUDE_DIR=$include" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
    "MPV_LIBRARY=$mpvLibrary" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
    "JFD_RUNTIME_DIR=$runtime" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
    $runtime | Out-File -Append -Encoding utf8 $env:GITHUB_PATH
}
