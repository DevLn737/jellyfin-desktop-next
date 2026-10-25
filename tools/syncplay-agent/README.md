# SyncPlay drift checks for our client

Our client's SyncPlay stack, the app's own session runtime (`src/session`:
`core`, `syncplay`, the `jellyfin-api` client and session WebSocket, the
launcher), in a live group on a Jellyfin 12.2 server together with the
[harness](../syncplay-harness/README.md) reference participant and Jellyfin
Web in headless Chromium. CI runs it on every pull request against the forked
server (playback-rate extension) and against stock 12.2, and posts the report
as one PR comment that is updated on every push.

## Pieces

- **`jfd_syncplay_agent`** ([main.cpp](main.cpp)) is a headless client:
  `SessionRuntime`, exactly as the app builds it, with a virtual file instead
  of mpv. It signs in, reports capabilities, keeps the session WebSocket,
  resolves the group's items through PlaybackInfo and leaves the group when
  it quits. The file plays on the wall clock and runs fast by
  `--speed-error-ppm` (as an audio device clock does), so drift correction
  has real work to do. No decoding: what it checks is the sync logic and its
  wiring, not the video pipeline (the Session workflow covers mpv).
- **`NativeParticipant`** ([nativeParticipant.mjs](nativeParticipant.mjs))
  starts the agent against a local TCP proxy to the server and reads
  positions from it in the harness's `sample()` shape. A network drop cuts
  every proxied connection and refuses new ones; a sleep freezes the agent
  and leaves its connections open but dead. The agent's own reconnect and
  wake detection bring it back.
- **Scenarios** ([scenarios.mjs](scenarios.mjs)) and the runner
  ([run.mjs](run.mjs)), which writes `drift-report.md` and `drift-report.json`.

| Scenario | Server | What happens | Pass condition |
| --- | --- | --- | --- |
| `native-drift` | fork | group plays at 1x, then our client asks for 1.25x and 1.5x | spread < 400 ms at 10/15/20 s at each rate |
| `native-cold-join` | fork | our client joins a group that already plays, then 1.5x | in sync after the join; spread < 400 ms at 10/15/20 s |
| `native-controls` | both | our client pauses, seeks while paused, resumes, quits | everyone follows; paused positions agree; drift after resume; it leaves the group on quit |
| `native-network-drop` | both | our client loses the network for 10 s while the group seeks | back in the group and in sync without user action |
| `native-sleep` | both | the computer sleeps 20 s (agent frozen, its connections left dead) while the group seeks | our client notices on waking, rejoins and is in sync without user action |
| `stock-server` | stock | 1x group; our client asks for 1.5x | SyncPlay works at 1x; the refusal reaches the app; nobody changes rate |
| `native-long` | with `--long N` | N minutes, 1x / 1.25x / 1.5x in turn | p95 of the spread, sampled every second, < 400 ms |

The thresholds are the stage 1 rows «Дрейф, автоматически», «Обрыв сети» and
«Обычный сервер без форка».

Web participants leave before the resume in `native-controls`: Web 12.2 cannot
resume after a paused seek (harness known issue 2), which is not ours to test.

## Mixed group with real playback

`jfd_syncplay_app_agent` ([app/appAgent.cpp](app/appAgent.cpp)) is our
client as the app runs it, without the window: headless mpv, the player
screen's model and the watch-together model. It speaks the same protocol
plus `watch` (Watch together on an item), `tracks`, `audio`, `subtitle`
and `skip` (the segment Skip button). [mixedGroup.mjs](mixedGroup.mjs)
runs it with Jellyfin Web on the extras stand
(`scripts/test-server/extras-server.sh`): the group starts from Watch
together, Web joins, our client plays the external audio file and moves
to a track Web is not on, and its Skip button moves everyone past the
intro, each step within the 400 ms drift threshold. The Session workflow
runs it on the stock server job.

```sh
cmake --build build-app --target jfd_syncplay_app_agent   # needs -DJFD_BUILD_APP=ON
eval "$(scripts/test-server/extras-server.sh up | sed 's/^/export /')"
node tools/syncplay-agent/mixedGroup.mjs --agent build-app/tools/syncplay-agent/app/jfd_syncplay_app_agent --out out/mixed
```

## Run it locally

```sh
# Needs Qt 6 with WebSockets
cmake -S . -B build -G Ninja -DJFD_REQUIRE_QT=ON && cmake --build build --target jfd_syncplay_agent
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

`jfd_syncplay_agent --server URL --user NAME --password PASS --device ID
[--no-rate] [--speed-error-ppm N]`, then one JSON object per line. The driver
sends `create`, `join`, `leave`, `intent` (`play`, `pause`, `seek`, `rate`,
`stop`, `next`, `previous`), `sample` and `quit`. The agent sends `ready`
(socket up, again after a reconnect), `disconnected`, `ws` (SyncPlay
messages), `request` (SyncPlay HTTP statuses), `sample`, `notice`,
`correction`, `log` and `fatal`. The header of [main.cpp](main.cpp) has the
fields.
