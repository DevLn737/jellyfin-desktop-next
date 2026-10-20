# Roadmap

Windows and Linux are equal targets. HDR is wanted on Windows first. macOS is
not planned. Test hardware: a desktop with an AMD RX 9060 XT, two monitors and
sometimes a TV, and a laptop with Intel Iris Xe as the second machine.

## Phase 0: foundation

- [x] Repository, license, ADRs 0001–0003.
- [x] CMake layout with the Qt-free `core` library and unit tests on Windows, Linux and macOS.
- [ ] CI on Windows and Linux builds an empty Qt Quick window linked with libmpv,
      with pinned Qt and libmpv versions.

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
| Menu frames over video | Desktop on Windows and Linux (Wayland, X11), laptop at 1080p60; 1080p60 and 4K60 SDR; 5 minutes of animated menu; frame intervals from app diagnostics | p95 interval ≤ 1.25 refresh periods |
| Memory | Same one-hour protocol for the prototype and 2.x | Growth after warm-up ≤ 64 MiB per hour **and** lower than 2.x |
| Fullscreen | 50 toggles across the desktop's two monitors and the TV | No geometry drift, no lost windows |
| Shutdown | Close during video and during a SyncPlay group | < 1 s, exit code 0 |
| A/V sync | mpv `avsync` at 1×, 1.25×, 1.5× and after rate corrections | abs(avsync) < 50 ms in steady state |
| First frame and seek | Command to first frame; seek to ready | Direct play on LAN: first frame < 2 s, seek < 1 s |
| Zero-copy decoding | `hwdec-current` | `d3d11va` on Windows, `vaapi` on Linux AMD/Intel |
| HDR on Windows | HDR10 clip on the desktop's HDR display with a menu over it | HDR signal and PQ output (`video-target-params`); interface at SDR white; tone mapping on SDR displays |
| CPU with hardware decoding | `hwdec-current` and CPU, 1080p and 4K | Hardware decoding active; CPU lower than 2.x on the same file |
| Drift, automated | Docker: 12.2 server, Jellyfin Web in headless Chromium, our client with a fake backend; one machine, so one clock | < 400 ms at 10, 15 and 20 s for 1×, 1.25×, 1.5×, correction on, cold second client; p95 < 400 ms over 30 minutes |
| Drift, real machines | Desktop and laptop; each client logs its logical position against server time (via `TimeSync`; Web through a page script) | Same targets |
| Network loss | One participant offline for 10 s | Rejoins and resynchronizes without user action |
| Stock server | Group on unmodified Jellyfin 12.2 | SyncPlay works at 1×; playback rate is not offered |
| Simulator | 10,000 randomized scenarios | Zero invariant violations, **and** each seeded defect (epoch check disabled, Ready with a stale position, late correction timer after leave) is caught; recorded server sequences replay cleanly |

## Phase 2a: what the group needs

Windows installer and Linux package (AppImage or Flatpak), server sign-in, Continue Watching, series, seasons and episodes, search, item
pages, SyncPlay group interface, track and external-audio selection, intro
skipping through the server's media segments, Russian translation from the
first screen, Linux platform modules (D-Bus sleep inhibition, MPRIS, Secret Service). After acceptance this replaces 2.x for the group.

## Phase 2b: the rest

Movies and filters, watched state, settings, updates, settings import from 2.x,
user-facing diagnostics.

## Phase 3: platforms and remaining features

HDR on Linux (mpv-window mode), music, gamepads and HDMI-CEC,
display refresh-rate switching, Live TV if needed, a public integration interface.

## Ongoing

- Porting the playback-rate extension in the server and Web forks to each new Jellyfin release.
- Proposing the extension upstream, separately and only with the owner's agreement.

## Answers (2026-10-20)

1. HDR is wanted.
2. Users are on Windows and Linux with AMD and NVIDIA GPUs; nobody uses macOS.
3. Test hardware: desktop with AMD RX 9060 XT 16 GB, two monitors and sometimes a TV; laptop with Intel Iris Xe.
   There is no NVIDIA test machine, so NVIDIA rows stay unmeasured until someone in the group runs the measurement script.
