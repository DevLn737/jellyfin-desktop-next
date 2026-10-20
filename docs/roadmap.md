# Roadmap

Defaults until the questions at the end are answered: no HDR, Windows first,
Linux in CI, macOS later.

## Phase 0: foundation

- [x] Repository, license, ADRs 0001–0003.
- [x] CMake layout with the Qt-free `core` library and unit tests on Windows, Linux and macOS.
- [x] CI on Windows and Linux builds an empty Qt Quick window linked with libmpv,
      with pinned Qt and libmpv versions ([deps/versions.json](../deps/versions.json)).

Phase 0 is not complete until the last item is done.

## Phase 1: prototype and go/no-go (6 weeks, riskiest part first)

| Weeks | Work | Checkpoint |
| --- | --- | --- |
| 1–2 | Qt and libmpv in CI. Minimal app: window, mpv below QML, animated menu over video, fullscreen, ordered shutdown, measurement scripts. The same measurements of the 2.x client on the same hardware as a baseline. In parallel: core interfaces, session and simulator. | Screen recording and numbers for both clients. If the new stack is not better than 2.x, stop and revisit [ADR 0001](adr/0001-stack.md). |
| 3–4 | `PlaybackSession`, `Reconciler`, `TimeSync`; SyncPlay client against the 12.2 server in Docker together with Jellyfin Web in headless Chromium; Jellyfin API client. | Automated protocol and drift report on every PR; screen recording. |
| 5–6 | Whole app: mpv backend in the session, transcoded HLS with an offset, external audio, group OSD, Discord. Mixed group on real machines. | Full threshold table and the go/no-go decision. |

A checkpoint is due every two weeks with a screen recording and numbers. Two
missed checkpoints in a row trigger a plan review.

### Targets

| Check | How it is measured | Target |
| --- | --- | --- |
| Menu frames over video | Group's GPUs; 1080p60 and 4K60 SDR; 5 minutes of animated menu; frame intervals from app diagnostics | p95 interval ≤ 1.25 refresh periods |
| Memory | Same one-hour protocol for the prototype and 2.x | Growth after warm-up ≤ 64 MiB per hour **and** lower than 2.x |
| Fullscreen | 50 toggles; two monitors with different DPI if available, otherwise one monitor at 100% and 150% in turn | No geometry drift, no lost windows |
| Shutdown | Close during video and during a SyncPlay group | < 1 s, exit code 0 |
| A/V sync | mpv `avsync` at 1×, 1.25×, 1.5× and after rate corrections | abs(avsync) < 50 ms in steady state |
| First frame and seek | Command to first frame; seek to ready | Direct play on LAN: first frame < 2 s, seek < 1 s |
| CPU with hardware decoding | `hwdec-current` and CPU, 1080p and 4K | Hardware decoding active; CPU lower than 2.x on the same file |
| Drift, automated | Docker: 12.2 server, Jellyfin Web in headless Chromium, our client with a fake backend; one machine, so one clock | < 400 ms at 10, 15 and 20 s for 1×, 1.25×, 1.5×, correction on, cold second client; p95 < 400 ms over 30 minutes |
| Drift, real machines | Each client logs its logical position against server time (via `TimeSync`; Web through a page script) | Same targets, if a second computer is available |
| Network loss | One participant offline for 10 s | Rejoins and resynchronizes without user action |
| Stock server | Group on unmodified Jellyfin 12.2 | SyncPlay works at 1×; playback rate is not offered |
| Simulator | 10,000 randomized scenarios | Zero invariant violations, **and** each seeded defect (epoch check disabled, Ready with a stale position, late correction timer after leave) is caught; recorded server sequences replay cleanly |

## Phase 2a: what the group needs

Server sign-in, Continue Watching, series, seasons and episodes, search, item
pages, SyncPlay group interface, track and external-audio selection, intro
skipping through the server's media segments, Russian translation from the
first screen, Windows installer. After acceptance this replaces 2.x for the group.

## Phase 2b: the rest

Movies and filters, watched state, settings, updates, settings import from 2.x,
user-facing diagnostics.

## Phase 3: platforms and remaining features

Linux packages (AppImage or Flatpak), macOS, music, gamepads and HDMI-CEC,
display refresh-rate switching, Live TV if needed, a public integration interface.

## Ongoing

- Porting the playback-rate extension in the server and Web forks to each new Jellyfin release.
- Proposing the extension upstream, separately and only with the owner's agreement.

## Open questions

1. Is HDR or 4K HDR needed?
2. Who will use the client, on which operating systems and GPUs?
3. Which hardware is available for testing: monitors, GPUs, a second computer for mixed groups?
