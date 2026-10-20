# Measurements on a real computer

Stage 1 compares the prototype with the 2.x client on the same Windows PC,
following the project's `architecture/measurement-protocol.md` (rev. 2).
`Measure-Prototype.ps1` mirrors the 2.x harness
(`scripts/baseline/Measure-Baseline.ps1` in DevLn737/jellyfin-desktop): same
scenarios, timings, statistics and mpv probe (`jfd-probe.lua`, a verbatim
copy), so the two JSON reports compare field by field.

GPU, frame and CPU numbers are only meaningful at the PC's own console: not
over Remote Desktop, nothing else running, the display at its normal refresh
rate. The report records whether the session was remote.

## Steps (Windows PowerShell 5.1 or 7)

1. Download the `jellyfin-desktop-next-windows` artifact of a green CI run and
   unpack it, for example to `C:\jfd\next`. The measurement scripts are in its
   `measure` folder.
2. Generate the media once (needs ffmpeg with libx264 and libx265). The 2.x
   harness uses the same files:

   ```powershell
   C:\jfd\next\measure\New-Fixtures.ps1 -OutDir C:\Users\win\Documents\Claude\jfd-media
   ```

3. Run everything (about 2 hours, most of it the one-hour memory run):

   ```powershell
   powershell -ExecutionPolicy Bypass -File C:\jfd\next\measure\Measure-Prototype.ps1 `
     -AppDir C:\jfd\next -MediaDir C:\Users\win\Documents\Claude\jfd-media -OutDir C:\jfd\next-out
   ```

   A short check first: `-Scenarios cpu-1080,shutdown -HardwareDecoding auto-safe`.

`prototype-report.json` holds the computer, every scenario's numbers and any
failures; each scenario's folder keeps the raw probe, the client log and the
process samples. The `recording` scenario writes `recording.mp4` for the pull
request.

## Scenarios

| Code | What happens | What is counted |
| --- | --- | --- |
| `frames-1080`, `frames-2160` | each file (H.264, HEVC) with the protocol overlay, 10 s settle, 5 min window | frame intervals from the Qt Quick render loop log: p50, p95, p99, max, share over 1.25 refresh periods; avsync and mpv drops from the probe |
| `cpu-1080`, `cpu-2160` | each file without overlay, 10 s settle, 60 s | CPU of the process tree (% of one core and of all cores), `hwdec-current` |
| `memory-hour` | 1080p60 with overlay, reloaded every 60 s, 120 s warm-up + 3600 s, sample every 5 s | growth = median of the last 5 min − median of the first 5 min (private bytes and working set), peak |
| `fullscreen-50` | 1080p60, 50 times into fullscreen and back with 1 s pauses, on every monitor | window rectangle before and after each pair, drift > 1 px |
| `first-frame-seek` | 5 loads of 1080p60, 20 seeks alternating 50% and 20% | `start-file` → `playback-restart`, `seek` → `playback-restart`: median and max |
| `shutdown` | 1080p60 with overlay for 5 s, then WM_CLOSE; 5 times | time until the process tree exits, exit code |
| `recording` | the app's menu over 1080p60, the protocol overlay, fullscreen | `recording.mp4` (gdigrab), not a measurement |

Renderer: `-Renderer auto` (the default) measures the app's default, Direct3D
11 on Windows; `-Renderer gl` measures the OpenGL fallback. The report records
which one ran, and `client.log` names the adapter (`video renderer: ...`).

HDR experiment: `-Hdr hdr10` asks for an HDR10 swap chain (Windows HDR must be
on for the screen). `client.log` then has `HDR10 output: SDR white ... nits,
display peak ... nits`, or Qt's warning that it fell back to SDR, and each
frames result records mpv's `targetParams` (primaries, transfer, peak).

Hardware decoding: `auto-safe` is the app's default (direct decoding where mpv
considers it safe) and `auto-copy` matches the 2.x client's default copy mode;
frames and CPU run once in each.

The harness talks to the app through mpv's JSON IPC on a named pipe
(`--mpv-option input-ipc-server=...`): `loadfile`, `seek`, probe marks, and
the app's script messages `jfd-fullscreen`, `jfd-overlay <menu|protocol|none>`
and `jfd-quit`.

## Not covered here

- First frame and seek are measured on a local file, not over the network.
- SyncPlay drift and network loss are measured by the core and test-server workstreams.
- In the cloud CI (software OpenGL, llvmpipe; WARP for Direct3D 11) mpv's default lanczos scaler
  sometimes draws black; bilinear never does. That is a Mesa software
  renderer issue and does not affect the measurements on a real GPU.
