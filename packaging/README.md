# Packages

Built by `.github/workflows/package.yml`; every version used is pinned in
[deps/versions.json](../deps/versions.json). The packed program is the
`jellyfin-desktop` target (src/desktop); the packages are named
`jellyfin-desktop-next`.

## Windows MSI

1. The app is built with the pinned Qt and libmpv (`scripts/fetch-windows-deps.ps1`).
2. `windeployqt` copies Qt, the QML modules and the MSVC runtime next to the
   executable; libmpv and the Vulkan loader are added, and the licences of
   everything bundled (`LICENSE`, `LICENSES.md`, [licenses/](licenses) with
   `THIRD-PARTY-NOTICES.txt`) go to a `licenses` folder. A DLL next to the
   executable that the notices do not cover stops the build. SDL3 (gamepads) needs no DLL: CMake builds the pinned
   release statically into the executable (`cmake/JfdSdl3.cmake`).
3. [windows/build-msi.ps1](windows/build-msi.ps1) builds
   [windows/jellyfin-desktop-next.wxs](windows/jellyfin-desktop-next.wxs) with
   WiX 7 (a .NET tool from NuGet; its binary EULA, the Open Source Maintenance
   Fee terms, is accepted with `-acceptEula wix7`, as for the 2.x client), then
   runs Windows Installer's validation.
4. [windows/test-msi.ps1](windows/test-msi.ps1) installs it silently, checks
   that it needs no elevation, the files, shortcuts and registry key, runs the
   installed app's smoke test with only Windows on `PATH`, reinstalls the same
   version, uninstalls, and checks that nothing is left (and that the profile is
   kept, or removed with `REMOVEPROFILE=1`). Uninstalling empties the install
   folder only when it is the default one: a folder chosen with
   `INSTALLFOLDER=`, or one the remembered registry value was changed to,
   keeps the files the installer did not put there (checked too).
5. [windows/test-msi-update.ps1](windows/test-msi-update.ps1) runs the
   client's own update script ([apply-update.ps1](../src/integrations/apply-update.ps1),
   built into `MsiUpdater`) against real packages: an older MSI of the same
   build is installed, the script waits for a process, upgrades it to the new
   MSI and starts the installed program again; a broken package must leave the
   `.failed` marker and the installed version, and the older MSI over the newer
   one must be refused as a downgrade.

**MSI versions.** Windows Installer orders packages by the first three fields of
`ProductVersion` only, so a pre-release cannot be a fourth field. The third
field is `patch * 1000 + rank`: 0 for a build without a release tag, `N` for
`-alpha.N`, `300 + N` for `-beta.N`, `600 + N` for `-rc.N` (N up to 299), 999 for
the release. `v0.0.1-beta.9` installs as 0.0.1309, `v0.0.1` as 0.0.1999. A newer
beta upgrades an older one, an older one is refused with the downgrade message,
and Apps & features shows the release's own version in its comments
(`ARPCOMMENTS`). [release-version.sh](release-version.sh) computes it (its `msi`
output) and refuses other pre-release names; `release_version_check` tests it.
Betas up to beta.8 were all built as 0.0.1 and are upgraded by any later one.

A release is published only after the AppImage passed its smoke test on every
distribution (`sign` needs `appimage-smoke`) and the commit's CI, App and
Session runs on main succeeded (the `release` job checks them).

To publish, run the Package workflow by hand on main with the tag
(`v0.0.1-beta.18`). Its optional `highlights` input is Markdown placed above the
generated change list, for a short summary of the release in Russian and
English.

The product name, folder and `UpgradeCode` in the `.wxs` identify installs:
never change the `UpgradeCode`. The profile folders removed by `REMOVEPROFILE=1`
are `%LOCALAPPDATA%\Jellyfin Desktop Next` and `%APPDATA%\Jellyfin Desktop Next`,
Qt's locations for the application name "Jellyfin Desktop Next" with no
organization name; keep them in step if the application's names change.

### libmpv for Windows: licences and source code

`libmpv-2.dll` is shinchiro/mpv-winbuild-cmake's build, pinned in
`deps/versions.json` (`windows.mpv`). It is GPL-3.0-or-later and links about
ninety libraries statically, so every release must carry their licence texts
and publish their source. The recipe builds each library from the head of a
branch without recording the commit, so
[`deps/libmpv-windows-sources.json`](../deps/libmpv-windows-sources.json)
records the revisions:

- where the DLL names a commit in a version string (FFmpeg, mpv, libass,
  libplacebo, dav1d, Opus, libvpx, libaom, SVT-AV1, SDL2), that commit;
- otherwise the branch's newest commit dated before the DLL's build time, and
  the source archive also keeps the branch's history 14 days back (a commit
  can reach the server later than its date: the build fetched the checked-out
  revision or one of those).

[`windows/libmpv-sources.py`](windows/libmpv-sources.py) `resolve` works the
revisions out from the recipe and the DLL's strings, `bundle` fetches them and
collects the licence files into
[`licenses/libmpv-windows.txt`](licenses/libmpv-windows.txt), which the MSI
ships. `.github/workflows/libmpv-sources.yml` checks both on every change and,
on main, publishes the release `libmpv-windows-<build>`: one archive per
library, the recipe, the licences, a README and a copy of the DLL's package
(upstream keeps only its 30 newest builds; `fetch-windows-deps.ps1` falls back
to the copy, `windows.mpv.mirror`). The package workflow publishes a client
release only when that release exists, and links it from the notes.

**New libmpv build:** change `windows.mpv` in `deps/versions.json` (with
`mirror` pointing at the new `libmpv-windows-<build>`), set `build`,
`recipe` (the recipe's last commit before the build) and `window.before` (the
DLL's build date: `strings libmpv-2.dll | grep -E '^[A-Z][a-z]{2} [ 0-9]{2} 20[0-9]{2} '`)
in the manifest, then run

    strings -n 6 libmpv-2.dll > strings.txt
    python3 packaging/windows/libmpv-sources.py resolve --recipe <recipe checkout> --strings strings.txt --write
    python3 packaging/windows/libmpv-sources.py bundle --out build/libmpv-sources --licenses-only
    cp build/libmpv-sources/libmpv-windows.txt packaging/licenses/

(or take both files from the workflow's output: it prints the resolved manifest
and keeps the collected licences as an artifact). After the merge the workflow
publishes the new source release before the next client release can go out.

## Release signing

Every package is signed with minisign in the `sign` job; the client installs
an update only when its `.minisig` verifies against a key built into it
(`JFD_RELEASE_PUBLIC_KEY`, and during a rotation `JFD_RELEASE_NEXT_PUBLIC_KEY`)
and its trusted comment names the file.

- Only release runs (a pushed `v*` tag, or a manual run on main with a tag)
  use the release key, the `MINISIGN_SECRET_KEY` secret, and they run in the
  `release` environment. Pull requests and plain pushes to main sign with a
  throwaway key made in the job. The key reaches minisign through a pipe and
  is never written to disk, and no script from the checkout runs in the step
  that holds it.
- Keep `MINISIGN_SECRET_KEY` as a secret of the `release` environment
  (Settings, Environments; GitHub creates the environment on the first release
  run), with deployment limited to `main` and `v*` tags, and delete the
  repository secret of that name. Until then the repository secret is used.
- The repository variable `MINISIGN_PUBLIC_KEY` is the public key of the
  current secret key; `sign` refuses a secret key that does not match it.

**Rotating the key.** Clients trust the keys they were built with, so a new
key must ship in a release before anything is signed with it:

1. Make a new key pair (`minisign -G -W -s next.key -p next.pub`) on a trusted
   machine. Set the repository variable `MINISIGN_NEXT_PUBLIC_KEY` to the
   second line of `next.pub`. Keep `next.key` offline.
2. Publish one or more releases as usual. They are still signed with the
   current key, and the clients they install trust both keys.
3. Once users have updated to such a release, switch: `MINISIGN_SECRET_KEY`
   (in the `release` environment) becomes the content of `next.key`,
   `MINISIGN_PUBLIC_KEY` becomes the new public key, and
   `MINISIGN_NEXT_PUBLIC_KEY` is cleared (or set to the key after it). Delete
   `next.key`. The next release is signed with the new key.

A client older than step 2 does not trust the new key: it reports a failed
signature check and links to the release page, where the user installs the
new version by hand once.

## Linux AppImage

1. [linux/build-libmpv.sh](linux/build-libmpv.sh) builds FFmpeg, libplacebo,
   libass and libmpv from pinned sources: mpv at the commit of the Windows
   build, so both systems play alike. FFmpeg is LGPL with VA-API and NVDEC.
   CI caches the result by the hash of the pins and the script.
2. The app is built against it with the pinned Qt. SDL3 for gamepads is
   built from its pinned release and linked statically (no libSDL3 in the
   image); it loads the system's libudev at run time and works without it.
   Display modes (refresh rate switching on X11) link libX11 and libXrandr,
   which libmpv already needs, so the image carries nothing new for them.
3. [linux/build-appimage.sh](linux/build-appimage.sh) packs it with linuxdeploy
   and its Qt plugin (X11 and Wayland). Libraries that must match the running
   system stay out: Mesa/GL/EGL/Vulkan, VA-API and VDPAU, libdrm/gbm, PipeWire,
   PulseAudio, ALSA, GnuTLS (it knows the distribution's CA store), Wayland,
   D-Bus, libudev. The licences of everything bundled go to
   `usr/share/licenses/jellyfin-desktop-next` (`THIRD-PARTY-NOTICES.txt` and
   the texts in [licenses/](licenses)); each library taken from the build
   system adds its Ubuntu package's copyright file under `system/`, and a
   library with neither stops the build. The AppImage excludelist also
   leaves fontconfig, FreeType and HarfBuzz to the system; FriBiDi, which it lists too, ships in the image,
   because systems without a GTK desktop often lack it. The GL dispatch
   library (libOpenGL.so.0 from libglvnd) must be installed; every desktop
   has it.
4. [linux/smoke-appimage.sh](linux/smoke-appimage.sh) runs the AppImage's smoke
   test (the window and every page on Xvfb with Mesa's software renderer) in Ubuntu
   24.04, Fedora and Arch containers, once as usual and once with SDL told
   not to use udev. First it lists every library the program needs from the
   system and is missing, so one run shows them all.

Build dependencies on Ubuntu 24.04 are in [linux/build-deps.txt](linux/build-deps.txt).

## Not covered by CI

Hardware decoding (VA-API, NVDEC, D3D11VA), HDR, real displays (CI switches
refresh rates only on Xvfb and only lists them on Windows), fractional
scaling, Wayland sessions and real gamepads (CI uses SDL's virtual joystick)
are checked on real machines.
