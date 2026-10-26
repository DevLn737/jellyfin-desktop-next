# Jellyfin Desktop Next

A cross-platform Jellyfin desktop client designed around watching together.
SyncPlay is part of the playback state model from the start, not an add-on.

**Status:** early development (phase 1 prototype). Nothing here is usable yet. The
current Windows client is [DevLn737/jellyfin-desktop](https://github.com/DevLn737/jellyfin-desktop).

## Goals

- Playback through libmpv: hardware decoding, ASS/PGS subtitles, external audio tracks.
- SyncPlay with synchronized position and speed, with each participant choosing
  their own audio and subtitles.
- A native Qt Quick interface. No embedded Jellyfin Web and no Chromium.
- Windows and Linux from the first build, with HDR on Windows.
- Discord Rich Presence and other integrations behind a small, stable interface.

## Architecture

The decisions and their reasons are recorded in [docs/adr](docs/adr):

1. [Stack: C++20, Qt 6 Quick, libmpv](docs/adr/0001-stack.md)
2. [Module boundaries](docs/adr/0002-module-boundaries.md)
3. [Playback session and SyncPlay model](docs/adr/0003-playback-session-model.md)

The plan and its exit criteria are in [docs/roadmap.md](docs/roadmap.md).

## Related repositories

| Repository | Role |
| --- | --- |
| [DevLn737/jellyfin](https://github.com/DevLn737/jellyfin/tree/syncplay-rate/12.2) | Server fork with the SyncPlay playback-rate extension |
| [DevLn737/jellyfin-web](https://github.com/DevLn737/jellyfin-web/tree/syncplay-rate/12.2) | Web fork with the same extension, for browser participants |
| [DevLn737/jellyfin-syncplay-speed](https://github.com/DevLn737/jellyfin-syncplay-speed) | Server image build, release manifest and protocol documentation |
| [DevLn737/jellyfin-desktop](https://github.com/DevLn737/jellyfin-desktop) | Current Windows client (maintenance only) |

## Testing SyncPlay

[tools/syncplay-harness](tools/syncplay-harness) runs the forked Jellyfin 12.2
server in Docker and checks SyncPlay drift, pause, seek, rate changes and
rejoin with Jellyfin Web clients in headless Chromium. CI runs it on every
change to the harness.

## Building

The Qt-free core always builds. The Qt modules (`platform`, `jellyfin-api`,
`library`) build when Qt 6.4+ with Qt WebSockets is found; see
[docs/jellyfin-api.md](docs/jellyfin-api.md).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.24+ and a C++20 compiler (MSVC 2022, GCC 12+, Clang 15+ / Apple Clang 15+).

The application also needs Qt 6 (Quick, OpenGL) and libmpv; add `-DJFD_BUILD_APP=ON`. On Windows it renders with Direct3D 11 when libmpv has the D3D11 render API (client API 2.6, as pinned), otherwise with OpenGL.
### Modules and translations

Each module is a directory under `src/` with its own `CMakeLists.txt`; the root
file lists every planned module and adds the ones that exist
([cmake/JfdModules.cmake](cmake/JfdModules.cmake)).

Interface strings are English in the source and translated per module in
`src/<module>/i18n/<module>_ru.ts` ([cmake/JfdTranslations.cmake](cmake/JfdTranslations.cmake)).
After changing strings, run `cmake --build build --target update_translations`,
translate the new entries and commit the `.ts` files. The `translations_check`
test fails on any string that is missing, untranslated or no longer used.

CI uses the versions pinned in [deps/versions.json](deps/versions.json) and passes
`-DJFD_ENFORCE_PINS=ON` to reject any other.

- **Linux:** libmpv from the distribution (`libmpv-dev`), found through pkg-config.
- **Windows:** from a Visual Studio developer PowerShell, `scripts/fetch-windows-deps.ps1`
  downloads and verifies the pinned Qt, libmpv and Vulkan loader; pass the Qt prefix
  as `CMAKE_PREFIX_PATH` and the `MPV_INCLUDE_DIR` and `MPV_LIBRARY` it prints to CMake.
  The Qt `bin` directory and `build/deps/runtime` must be on `PATH` when running.

Any platform can point CMake at a specific libmpv with `-DMPV_INCLUDE_DIR=... -DMPV_LIBRARY=...`.

## Packages

CI builds both packages on every pull request (`.github/workflows/package.yml`)
and keeps them as workflow artifacts for 30 days. Nothing is published as a
release yet. See [packaging/README.md](packaging/README.md) for how they are
built.

- **Windows:** `jellyfin-desktop-next-<version>-x64.msi`. Installs for the
  current user without administrator rights into
  `%LOCALAPPDATA%\Programs\Jellyfin Desktop Next`, with Start menu and desktop
  shortcuts. Windows 10 1903 or newer, 64-bit. The installer is **not signed**
  (there is no code-signing certificate), so SmartScreen warns on first run:
  choose "More info", then "Run anyway". Uninstalling removes the program and
  shortcuts and keeps settings; `msiexec /x <msi> REMOVEPROFILE=1` removes
  settings and cache too.
- **Linux:** `jellyfin-desktop-next-x86_64.AppImage`, one file for all
  distributions with glibc 2.39 or newer: Ubuntu 24.04, Fedora 40, Debian 13,
  Arch and newer. It carries Qt and the same libmpv build (mpv commit, FFmpeg)
  as Windows; graphics drivers, VA-API, PipeWire/PulseAudio and GnuTLS come
  from the system. Make it executable and run it. Flatpak comes later.

**Updates.** Both builds check for a newer release (Settings, Integrations)
and update themselves. The client downloads the release file and its
`.minisig`, checks the minisign signature against the key built into the
client, and offers "Restart and update" in the toast once nothing plays and
you are not in a SyncPlay group; if the download or the check fails, the toast
links to the release page instead.

- **Linux:** the AppImage is replaced in place (the folder must be writable).
  The old file is kept until the new version confirms its start; if it exits
  or hangs within 60 seconds, the old file is put back and started, and it
  reports that the new version did not start.
- **Windows:** `jellyfin-desktop-next-<version>-x64.msi` is installed over the
  running copy with `msiexec` (a major upgrade of the same product; per user,
  no administrator rights) by a PowerShell script that waits for the app to
  exit and starts it again. If the installer fails, Windows Installer restores
  the old version, which is started and reports that the new version did not
  install (the installer log is `update-install.log` in the app's
  `update` data folder). Only a copy installed by the MSI updates itself.

## License

GPL-2.0-or-later. Parts of this client are derived from
[Jellyfin Media Player](https://github.com/jellyfin/jellyfin-desktop).
