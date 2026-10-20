# Measurements on a real computer

Phase 1 weeks 1–2 compare the prototype with the 2.x client on the group's own
hardware (thresholds in [docs/roadmap.md](../../docs/roadmap.md)). GPU and frame
numbers are only meaningful when the computer is used locally: not over Remote
Desktop, nothing else running, the display at its normal refresh rate.

## Steps (Windows, PowerShell 5.1 or 7)

1. Download the `jellyfin-desktop-next-windows` artifact of a green CI run of
   `main` and unpack it, for example to `C:\jfd\app`.
2. Get ffmpeg (any recent build with libx264, libx265 and gdigrab).
3. Generate the fixtures once; the 2.x baseline uses the same files:

   ```powershell
   .\scripts\measure\New-Fixtures.ps1 -OutDir C:\jfd\fixtures -Ffmpeg C:\jfd\ffmpeg.exe
   ```

4. Check that everything runs (about 10 minutes, numbers not valid):

   ```powershell
   .\scripts\measure\Measure-Prototype.ps1 -AppDir C:\jfd\app -FixtureDir C:\jfd\fixtures `
     -OutDir C:\jfd\quick -Ffmpeg C:\jfd\ffmpeg.exe -Quick
   ```

5. The full run takes about 85 minutes, most of it the one-hour memory run:

   ```powershell
   .\scripts\measure\Measure-Prototype.ps1 -AppDir C:\jfd\app -FixtureDir C:\jfd\fixtures `
     -OutDir C:\jfd\full -Ffmpeg C:\jfd\ffmpeg.exe
   ```

`summary.json` holds the computer, every run's report and a pass/fail line per
threshold; `recording.mp4` is the screen recording for the pull request.

## What each run does

| Run | App arguments | Measures |
| --- | --- | --- |
| `frames-1080p60`, `frames-2160p60` | `--scenario frames --fullscreen --animate-menu --duration 300` | Interval between buffer swaps (p50/p95/p99, intervals over 1.5 refresh periods), mpv dropped/delayed frames, `avsync`, CPU of the process, `hwdec-current` |
| `frames-1080p60-software` | same with `--hwdec no`, 60 s | CPU without hardware decoding, for comparison |
| `seek` | `--scenario seek --seeks 20` | Time from seek command to playback restart |
| `fullscreen` | `--scenario fullscreen --toggles 50` | Window geometry after each round trip, on every monitor in turn |
| `shutdown` | `--scenario play --animate-menu`, closed after 15 s | Time from the close message (as a user's click) to process exit, exit code |
| `recording` | `--scenario play --stats` | 40 s screen recording while the menu and fullscreen are used |
| `memory` | `--scenario memory --duration 3600 --replace-interval 60` | Private bytes every 5 s inside the app and for the process tree from outside; growth after a 120 s warm-up |

`MeasureCommon.psm1` has the process-tree sampler and the close-and-time
helper. They look at a process only from outside, so the same functions
measure the 2.x client (and its QtWebEngine helper processes).

## Not covered here

- First frame and seek are measured on a local file, not over the network.
- SyncPlay drift and network loss are measured by the core and test-server workstreams.
