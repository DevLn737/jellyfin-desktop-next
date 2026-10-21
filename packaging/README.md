# Packages

Built by `.github/workflows/package.yml`; every version used is pinned in
[deps/versions.json](../deps/versions.json). The packed program is the
`jellyfin-desktop` target (src/desktop); its file is `jellyfin-desktop-next`,
so it never clashes with the official client.

## Windows MSI

1. The app is built with the pinned Qt and libmpv (`scripts/fetch-windows-deps.ps1`).
2. `windeployqt` copies Qt, the QML modules and the MSVC runtime next to the
   executable; libmpv and the Vulkan loader are added.
3. [windows/build-msi.ps1](windows/build-msi.ps1) builds
   [windows/jellyfin-desktop-next.wxs](windows/jellyfin-desktop-next.wxs) with
   WiX 7 (a .NET tool from NuGet; its binary EULA, the Open Source Maintenance
   Fee terms, is accepted with `-acceptEula wix7`, as for the 2.x client), then
   runs Windows Installer's validation.
4. [windows/test-msi.ps1](windows/test-msi.ps1) installs it silently, checks
   that it needs no elevation, the files, shortcuts and registry key, runs the
   installed app's smoke test with only Windows on `PATH`, reinstalls the same
   version, uninstalls, and checks that nothing is left (and that the profile is
   kept, or removed with `REMOVEPROFILE=1`).

The product name, folder and `UpgradeCode` in the `.wxs` identify installs:
never change the `UpgradeCode`. The profile folders removed by `REMOVEPROFILE=1`
are `%LOCALAPPDATA%\Jellyfin Desktop Next` and `%APPDATA%\Jellyfin Desktop Next`,
Qt's locations for the application name "Jellyfin Desktop Next" with no
organization name; keep them in step if the application's names change.

## Linux AppImage

1. [linux/build-libmpv.sh](linux/build-libmpv.sh) builds FFmpeg, libplacebo,
   libass and libmpv from pinned sources: mpv at the commit of the Windows
   build, so both systems play alike. FFmpeg is LGPL with VA-API and NVDEC.
   CI caches the result by the hash of the pins and the script.
2. The app is built against it with the pinned Qt.
3. [linux/build-appimage.sh](linux/build-appimage.sh) packs it with linuxdeploy
   and its Qt plugin (X11 and Wayland). Libraries that must match the running
   system stay out: Mesa/GL/EGL/Vulkan, VA-API and VDPAU, libdrm/gbm, PipeWire,
   PulseAudio, ALSA, GnuTLS (it knows the distribution's CA store), Wayland,
   D-Bus. The AppImage excludelist also leaves fontconfig, FreeType and
   HarfBuzz to the system; FriBiDi, which it lists too, ships in the image,
   because systems without a GTK desktop often lack it. The GL dispatch
   library (libOpenGL.so.0 from libglvnd) must be installed; every desktop
   has it.
4. [linux/smoke-appimage.sh](linux/smoke-appimage.sh) runs the AppImage's smoke
   test (the window and every page on Xvfb with Mesa's software renderer) in Ubuntu
   24.04, Fedora and Arch containers. First it lists every library the
   program needs from the system and is missing, so one run shows them all.

Build dependencies on Ubuntu 24.04 are in [linux/build-deps.txt](linux/build-deps.txt).

## Not covered by CI

Hardware decoding (VA-API, NVDEC, D3D11VA), HDR, real displays, fractional
scaling and Wayland sessions are checked on real machines.
