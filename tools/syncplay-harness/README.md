# SyncPlay test harness

A reproducible Jellyfin server built from the SyncPlay playback-rate forks
(Server and Web 12.2) and automated checks of SyncPlay behaviour in a mixed
group. It runs on any Linux machine with Docker and Node 22, in CI on every
change under this folder, and in cloud sandboxes, so SyncPlay can be checked
without anyone's computer.

What it checks, all participants on one machine so they share a clock:

| Scenario | What happens | Pass condition |
| --- | --- | --- |
| `websocket-auth` | WebSocket auth the way Web 12.2 does it | documents a known incompatibility (below) |
| `drift-1x` | driver + 2 Web clients start together at 1x | everyone playing; spread < 400 ms at 10/15/20 s |
| `drift-rates` | group rate 1.25x, then 1.5x, then back to 1x while playing | everyone at the new rate; spread < 400 ms at 10/15/20 s |
| `pause-seek` | Web pauses, seek to 30 s while paused, rate change while paused, resume | everyone paused at the same position; pause survives seek and rate change |
| `seek-paused-resume` | pause, seek, wait, resume | the group resumes |
| `leave-rejoin` | a Web client leaves and rejoins a playing group | it follows the group again |
| `legacy-participant` | a client without `SupportsSyncPlayPlaybackRate` | 409 on join to 1.5x and on a rate change, group state unchanged |

The 400 ms limit and the checkpoints come from `integratedAcceptanceCases` in
the 2.x client's `contracts/desktop-api-contract.json`.

## Run it

```sh
cd tools/syncplay-harness
npm ci
npx playwright install chromium     # skip where Playwright's Chromium is preinstalled
scripts/server-up.sh                # builds the image on first run, starts it, bootstraps
node src/run.mjs --out out          # out/report.md and out/report.json; exit 1 on failure
node src/run.mjs --only drift-1x    # one scenario
scripts/server-down.sh
```

The first image build compiles the Server fork (.NET 10) and the Web fork
(Node 24) inside Docker and takes several minutes; later builds reuse the Docker cache. Sandboxes that cannot
download GitHub archive tarballs (one Web dependency is one) can use the
payloads CI publishes from `main` instead: `PAYLOAD=release scripts/server-up.sh`.
`PAYLOAD=/dir` uses `/dir/server` and `/dir/web` built some other way.

The server listens on `http://localhost:8096` (`PORT` to change). Accounts:
`admin`/`admin` and `alice`, `bob`, `carol`, `dave`, each with its name as the
password. The library holds one item, *SyncPlay Test Clip*.

## The server image

[`server/Dockerfile`](server/Dockerfile) builds the pinned fork commits
(`SERVER_REF`, `WEB_REF`, the same ones as the
[release manifest](https://github.com/DevLn737/jellyfin-syncplay-speed/blob/main/release-manifest.json))
and places them on top of the official `jellyfin/jellyfin` 12.2 image, which
supplies the runtime, jellyfin-ffmpeg and fonts. It is a test image: no GPU
runtimes, no persistent volumes.

The bootstrap turns `EnableLegacyAuthorization` on. See the first known issue.

### Test clip

[`server/make-media.sh`](server/make-media.sh) generates a 10-minute,
640×360, 24 fps clip (VP9 + Opus in WebM, so open-source Chromium plays it
directly). Each frame carries its frame number as a 16-bit barcode across the
top 40 pixels: 16 cells of 40×40 px, least significant bit on the left,
white = 1. Below it the time and frame number are printed for people.

## Participants

- **ProtocolParticipant** ([src/protocolClient.mjs](src/protocolClient.mjs)) speaks
  the SyncPlay protocol directly and plays on its own clock: NTP-style clock
  offset from `GetUtcTime`, commands applied at their `When`, Ready after
  loads and seeks. It drives the group and is the exact reference. It is also
  a compact, readable model of what a native client has to do.
- **WebParticipant** ([src/webParticipant.mjs](src/webParticipant.mjs)) is the
  forked Jellyfin Web in headless Chromium. It logs in through the real form,
  sends group requests through its own session, and its position is read from
  the `<video>` element. Sync correction is on, as in the acceptance cases.
- **ScreenParticipant** ([src/screenParticipant.mjs](src/screenParticipant.mjs))
  reads the barcode of whatever a client shows on an X display (Xvfb), so any
  native client can be measured without hooks. The client joins the group by
  itself; you give the on-screen position and cell size of the strip.

## Testing a native client

1. Start the server, run the client against `http://localhost:8096` as `dave`.
2. Under Xvfb, measure it with `ScreenParticipant`, or, if the client can
   report its position, add a participant class with the same `sample()`
   shape: `{ at, positionMs, playing, rate }` with `at` from `Date.now()`.
3. Reuse `startGroup`, `measureDrift` and `waitUntil` from
   [src/scenarios.mjs](src/scenarios.mjs) and [src/measure.mjs](src/measure.mjs).

## Known issues found by the harness

These are reported as ⚠️ and do not fail the run (see `KNOWN_ISSUES` in
[src/scenarios.mjs](src/scenarios.mjs)). When one stops reproducing, the
report says so; remove it then.

1. **Web 12.2 WebSocket is rejected by a default 12.2 server.** The server's
   `DisableLegacyAuthorization` migration turns legacy authorization off, but
   Web 12.2 still uses `jellyfin-apiclient` 1.11.0, which opens the socket
   with `?api_key=`. The server answers 403, so Web receives no SyncPlay
   commands. Web SyncPlay works only where `EnableLegacyAuthorization` is
   turned back on.
2. **A seek while paused can freeze the group.** Observed: about 3 s after a
   paused seek both Web clients report Buffering and never Ready. Likely cause
   from the code: Web's SyncPlay seek briefly
   unpauses, seeks and pauses again. The `<video>` element's `waiting` event
   starts a 3 s timer in `ui/players/HtmlVideoPlayer.js` that is cleared only
   by `playing`, which never comes while paused. Web then reports Buffering
   and never Ready, the group stays in Waiting, and Unpause does nothing. A
   group rate change is a seek, so it is affected too.
3. **Leave and rejoin in the same page freezes the group.** After leaving,
   Web keeps the last play-queue update; on rejoin `QueueCore` drops the
   group's queue as "ignoring old update" (same `LastUpdate`), never reports
   Ready, and the group stays in Waiting.
