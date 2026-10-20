# Roadmap

## Phase 0: foundation

- [x] Repository, license, ADRs 0001–0003.
- [x] CMake layout with the Qt-free `core` library and unit tests.
- [x] CI building and testing on Windows, Linux and macOS.
- [ ] Qt 6 and libmpv dependencies pinned per platform.

## Phase 1: prototype and go/no-go (4–6 weeks)

In scope:

- `core`: `PlaybackSession`, epochs, time types, `Reconciler`, deterministic simulator.
- `jellyfin-api`: sign-in, choosing an item, `PlaybackInfo` with a device profile, playback reporting.
- `syncplay`: create/join/leave/rejoin, Ready/Buffering, pause, seek, playback rate, `TimeSync`.
- `media-backend`: mpv with hardware decoding, ASS subtitles, external audio, transcoded HLS with an offset.
- `ui`: player screen with an on-screen display over video, fullscreen.
- Discord as the first integration.
- Builds that run on Windows and Linux; a composition check on macOS.

Out of scope: library browsing, settings screens, installer, updates, gamepads and CEC.

Continue only if all of these hold:

| Check | Target |
| --- | --- |
| SyncPlay drift, mixed group with Jellyfin Web and the 2.x client, 1×/1.25×/1.5×, correction on, cold second client | below 400 ms at 10, 15 and 20 s; p95 below 400 ms over 30 minutes |
| Simulator | zero invariant violations over 10,000 randomized scenarios |
| Memory | one-hour playback with an animated on-screen display grows less than the 2.x client on the same protocol, and well under 128 MiB after warm-up |
| Frame pacing | on-screen display over 1080p60 and 4K60 on AMD, NVIDIA and Intel: p95 frame interval at most 1.25 refresh periods |
| Fullscreen | 50 toggles across two monitors with different DPI, no geometry drift |
| Shutdown | closing during video and SyncPlay takes under 1 s and exits with code 0 |
| Effort | no more than 1.5× the planned time |

If these are missed, revisit [ADR 0001](adr/0001-stack.md) before continuing.

## Phase 2: watching together (2–3 months)

Server and user selection, home, movies, series, seasons and episodes, search,
item pages, resume and watched state, next episode, SyncPlay group interface,
track and external-audio selection, basic settings, Discord, diagnostics,
Windows installer and updates, settings import from the 2.x client. After
acceptance this client replaces 2.x for the group.

## Phase 3: platforms and remaining features

Linux (AppImage or Flatpak) and macOS packages, music, gamepads and HDMI-CEC,
display refresh-rate switching, Live TV if needed. A public integration
interface once two or three integrations use it.

## Later, separately

Proposing the SyncPlay playback-rate extension to upstream Jellyfin. If
accepted, the server and Web forks are no longer needed.
