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
- **Platforms:** Windows first, Linux in CI from the start, macOS later. The Qt-free core
  keeps building on all three.

## Cost of this choice

libmpv's render API supports only OpenGL and software rendering, so the whole
Qt Quick scene runs on OpenGL on every platform (the 2.x client already forces
it with `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)`). Qt 6
would otherwise use D3D11 on Windows and Metal on macOS. As a result:

- there is no HDR output;
- on Windows, hardware decoding often copies frames back (`d3d11va-copy`) instead of zero-copy;
- on macOS, OpenGL is deprecated.

HDR is out of scope for the prototype.

## Reasons

- Removing Chromium from the playback path removes the largest class of
  observed problems. Removing Qt does not address any observed problem.
- The proven renderer, timeline, readiness, external-audio, Discord IPC and
  platform modules can be ported instead of rewritten.
- One language keeps the build, debugging and contributor requirements simple.
- Subtitles, external audio, hardware decoding and audio sync come from mpv
  whatever the UI stack is.

## Rejected for now

- **Fathom** (Flutter, media_kit/libmpv, SyncPlay, AGPL-3.0) as a base: young,
  no macOS, no playback-rate extension, and AGPL-3.0 code cannot come back into
  a GPL-2.0-or-later client. A separate check of it against the 12.2 server is
  under way; its results will be added here.
- **Flutter with media_kit** as a stack: the proven code to port is C++/Qt, and
  a second stack would mean rewriting what already works. Fladder (Flutter,
  GPL-3.0) is a useful reference for media segments and trickplay.

- **Rust (with Slint, iced/egui or cxx-qt):** none of the observed problems is a
  memory-safety problem. Rust would add a second language or require a new,
  unproven mpv composition path.
- **CEF, Tauri or Electron with our own web UI:** removes the dependency on the
  server's Web, but keeps Chromium composited over video.
- **mpv in its own window (`--wid`) with a separate UI layer:** gives the native
  D3D11/Vulkan/Metal paths and better HDR, but brings back per-platform window,
  focus and fullscreen problems.

## Revisit when

- weeks 1–2 of phase 1 show that Qt Quick with mpv is not better than the 2.x
  client on frame pacing and memory on the group's hardware. The first fallback
  is a native player screen inside the 2.x client (Jellyfin Web hidden during
  playback); the second is comparing mpv in its own window (`--wid`) and other stacks;
- HDR passthrough or zero-copy hardware decoding on Windows becomes a requirement;
- the long-term maintainers are substantially more fluent in Rust than in C++.
