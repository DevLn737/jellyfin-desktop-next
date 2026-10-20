# SyncPlay drift checks for our client

Our client's SyncPlay logic, the real `core` and `syncplay` libraries, in a
live group on a Jellyfin 12.2 server together with the
[harness](../syncplay-harness/README.md) reference participant and Jellyfin
Web in headless Chromium. CI runs it on every pull request against the forked
server (playback-rate extension) and against stock 12.2, and posts the report
as one PR comment that is updated on every push.

## Pieces

- **`jfd_syncplay_agent`** ([main.cpp](main.cpp)) is a headless client:
  `PlaybackSession` + `GroupClient` on a real monotonic clock, with a virtual
  file instead of mpv. The file plays on the wall clock and runs fast by
  `--speed-error-ppm` (as an audio device clock does), so drift correction has
  real work to do. No decoding: what it checks is the sync logic, not the
  video pipeline.
- **`NativeParticipant`** ([nativeParticipant.mjs](nativeParticipant.mjs))
  carries the agent's HTTP requests and the session WebSocket (the job the Qt
  app's `jellyfin-api` module does) and reads positions from it in the
  harness's `sample()` shape. It can drop the network for a while.
- **Scenarios** ([scenarios.mjs](scenarios.mjs)) and the runner
  ([run.mjs](run.mjs)), which writes `drift-report.md` and `drift-report.json`.

| Scenario | Server | What happens | Pass condition |
| --- | --- | --- | --- |
| `native-drift` | fork | group plays at 1x, then our client asks for 1.25x and 1.5x | spread < 400 ms at 10/15/20 s at each rate |
| `native-cold-join` | fork | our client joins a group that already plays, then 1.5x | in sync after the join; spread < 400 ms at 10/15/20 s |
| `native-controls` | both | our client pauses, seeks while paused, resumes | everyone follows; paused positions agree; drift after resume |
| `native-network-drop` | both | our client loses the network for 10 s while the group seeks | back in the group and in sync without user action |
| `stock-server` | stock | 1x group; our client asks for 1.5x | SyncPlay works at 1x; the refusal reaches the app; nobody changes rate |
| `native-long` | with `--long N` | N minutes, 1x / 1.25x / 1.5x in turn | p95 of the spread, sampled every second, < 400 ms |

The thresholds are the stage 1 rows «Дрейф, автоматически», «Обрыв сети» and
«Обычный сервер без форка».

Web participants leave before the resume in `native-controls`: Web 12.2 cannot
resume after a paused seek (harness known issue 2), which is not ours to test.

## Run it locally

```sh
cmake -S . -B build -G Ninja && cmake --build build --target jfd_syncplay_agent
(cd tools/syncplay-harness && npm ci)

# Forked server (see the harness README; PAYLOAD=release in sandboxes)
tools/syncplay-harness/scripts/server-up.sh
node tools/syncplay-agent/run.mjs --agent build/tools/syncplay-agent/jfd_syncplay_agent --server fork --out out/fork

# Stock 12.2
PORT=8097 tools/syncplay-agent/scripts/stock-server-up.sh
node tools/syncplay-agent/run.mjs --agent build/tools/syncplay-agent/jfd_syncplay_agent \
  --server stock --url http://localhost:8097 --out out/stock
```

Options: `--web N` Web participants (default 1, 0 runs without Chromium),
`--speed-error-ppm` (default 3000), `--only a,b`, `--long minutes`.

The long run is not part of pull requests: it runs weekly and on demand
(`workflow_dispatch` with `long_minutes`).

## Agent protocol

One JSON object per line. The driver sends `start`, `create`, `join`,
`leave`, `intent` (`play`, `pause`, `seek`, `rate`, `stop`), `ws` (a raw
WebSocket message), `connection` (`lost`, `restored`), `reply` (to an `http`
request) and `sample`. The agent sends `http`, `sample`, `notice`,
`correction` and `log`. The header of [main.cpp](main.cpp) has the fields.
