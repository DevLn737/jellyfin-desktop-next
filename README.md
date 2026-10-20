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
- Windows, Linux and macOS from the first build.
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

## Building

The Qt-free core library and its tests:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.24+ and a C++20 compiler (MSVC 2022, GCC 12+, Clang 15+ / Apple Clang 15+).

The application also needs Qt 6 (Quick, OpenGL) and libmpv; add `-DJFD_BUILD_APP=ON`.
CI uses the versions pinned in [deps/versions.json](deps/versions.json) and passes
`-DJFD_ENFORCE_PINS=ON` to reject any other.

- **Linux:** libmpv from the distribution (`libmpv-dev`), found through pkg-config.
- **Windows:** from a Visual Studio developer PowerShell, `scripts/fetch-windows-deps.ps1`
  downloads and verifies the pinned libmpv and Vulkan loader, then pass the
  `MPV_INCLUDE_DIR` and `MPV_LIBRARY` it prints to CMake. `build/deps/runtime` must be on
  `PATH` when running.

Any platform can point CMake at a specific libmpv with `-DMPV_INCLUDE_DIR=... -DMPV_LIBRARY=...`.

## License

GPL-2.0-or-later. Parts of this client are derived from
[Jellyfin Media Player](https://github.com/jellyfin/jellyfin-desktop).
