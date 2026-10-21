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
- **Video:** libmpv through its render API, drawn below the QML scene: Direct3D 11 on
  Windows, OpenGL on Linux. The OpenGL renderer is also the Windows fallback.
- **Build:** CMake. CI builds and tests on Windows, Linux and macOS from the first commit.
- **Server admin and rarely used pages** open in the system browser instead of being reimplemented.
- **Platforms:** Windows and Linux as equal targets from the start. macOS is not planned
  while nobody in the group uses it; the Qt-free core keeps building there.

## Rendering path and HDR

Revised 2026-10-20: HDR is wanted, and the group uses Windows and Linux with
AMD and NVIDIA GPUs.

mpv's main branch has a Direct3D 11 render API (`MPV_RENDER_API_TYPE_D3D11`,
`include/mpv/render_d3d11.h`, client API 2.6, planned for mpv 0.42.0, not yet
tagged). mpv renders into a D3D11 texture on the application's device,
`d3d11va` decodes on the same device, and 10-bit and FP16 formats are supported.
The only other render APIs are OpenGL and software.

- **Windows:** Qt Quick on its default D3D11 backend, mpv through the D3D11
  render API into a texture below QML. This gives zero-copy `d3d11va` and a
  path to HDR: an HDR10 or scRGB swap chain with the interface composited at
  SDR white. Whether `QQuickWindow` can provide that swap chain or a custom one
  through `QQuickRenderControl` is needed is the first experiment of phase 1.
- **Linux:** Qt Quick on OpenGL (EGL), mpv through the OpenGL render API, with
  VA-API or NVDEC interop. Wayland and X11. No HDR: the OpenGL render API has no
  HDR output, and mpv's window cannot be embedded on Wayland (`--wid` is X11
  and Windows only). If HDR on Linux is needed later, the option is a separate
  fullscreen mode where mpv draws into its own window (Vulkan with Wayland
  color management) and the controls are drawn with mpv's OSD.

Costs:

- two renderers (D3D11 and OpenGL) behind one video item interface, with shared composition tests;
- an unreleased mpv on Windows: libmpv is built from a pinned main-branch commit until 0.42.0 ships;
- the Linux interface runs on OpenGL and has no HDR.

### HDR decision (2026-10-21)

The phase 1 experiment answered the swap chain question: `QQuickWindow`
provides an HDR10 swap chain (`QSG_RHI_HDR=hdr10`), with no custom render
control needed. mpv renders PQ into an `R10G10B10A2` texture, and one layer
re-encodes the interface from sRGB to PQ at the SDR white level Windows
reports. It builds in CI, and without an HDR display it falls back to SDR and
plays (`app_smoke_hdr_fallback`). It has not yet run on an HDR display: none
was enabled at the measurement sitting.

Decision: the first release is SDR on both platforms. mpv tone-maps HDR
sources to the SDR display. HDR10 output on
Windows stays in the code as an opt-in mode (`--hdr hdr10`). It becomes a
setting once it is checked on an HDR display, together with these known limits:

- the menu's blurred backdrop samples PQ video, so its colours are off;
- translucent panels blend in PQ space;
- the interface layer costs one extra full-window pass.

Measurements on the group's hardware (frames, CPU, first frame, fullscreen,
memory, and HDR on a TV) move to a pre-release check instead of gating the
interface work. The first run on the main PC (Direct3D 11, RX 9060 XT) already
showed 9–11 % of one core for 1080p H.264/HEVC (the 2.x client: 14–15 %),
no dropped frames, A/V sync p95 under 1 ms, and clean shutdowns of about
350 ms (2.x: 365 ms).

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
- the D3D11 path fails to deliver HDR or stable frame pacing on Windows in weeks 1–2:
  Windows then uses the OpenGL renderer (SDR) and HDR moves to a separate mpv-window mode;
- the long-term maintainers are substantially more fluent in Rust than in C++.
