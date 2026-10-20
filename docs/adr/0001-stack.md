# ADR 0001: C++20, Qt 6 Quick without WebEngine, libmpv

Status: accepted, 2026-10-20. Revisit after the phase 1 prototype.

## Context

The current client ([DevLn737/jellyfin-desktop](https://github.com/DevLn737/jellyfin-desktop))
hosts the server's Jellyfin Web in Qt WebEngine and draws transparent
Chromium over native mpv video. Its Qt 6 migration found:

- memory above 2 GiB within seconds with GPU WebEngine composition, reproduced
  without any video (`docs/windows-playback-composition-fix.md` there);
- retained opaque WebEngine backgrounds hiding video, and expensive software
  drawing of Jellyfin Web's backdrops and card layers;
- SyncPlay and the player state owned by the served Web client and reached
  through private fields (`playbackManager._playQueueManager.queueCore.manager`).

Most other defects (fullscreen restoration, recursive shutdown, mpv render
timing) were implementation bugs in window and player lifecycles. A different
language would not have prevented them.

The same repository has a working and tested composition path: mpv renders
through the OpenGL render API into a reusable FBO drawn below QML
(`src/player/PlayerQuickItem.cpp`, `tests/render/`).

## Decision

- **Language:** C++20 for the whole client.
- **UI:** Qt 6 Quick/QML. Qt WebEngine is not used.
- **Video:** libmpv through its render API, drawn below the QML scene.
- **Build:** CMake. CI builds and tests on Windows, Linux and macOS from the first commit.
- **Server admin and rarely used pages** open in the system browser instead of being reimplemented.

## Reasons

- Removing Chromium from the playback path removes the largest class of
  observed problems. Removing Qt does not address any observed problem.
- The proven renderer, timeline, readiness, external-audio, Discord IPC and
  platform modules can be ported instead of rewritten.
- One language keeps the build, debugging and contributor requirements simple.
- Subtitles, external audio, hardware decoding and audio sync come from mpv
  whatever the UI stack is.

## Rejected for now

- **Rust (with Slint, iced/egui or cxx-qt):** none of the observed problems is a
  memory-safety problem. Rust would add a second language or require a new,
  unproven mpv composition path.
- **CEF, Tauri or Electron with our own web UI:** removes the dependency on the
  server's Web, but keeps Chromium composited over video.
- **mpv in its own window (`--wid`) with a separate UI layer:** gives the native
  D3D11/Vulkan/Metal paths and better HDR, but brings back per-platform window,
  focus and fullscreen problems.

## Revisit when

- the phase 1 prototype misses its frame-pacing, memory or fullscreen targets with Qt Quick;
- HDR passthrough or zero-copy hardware decoding on Windows becomes a requirement;
- the long-term maintainers are substantially more fluent in Rust than in C++.
