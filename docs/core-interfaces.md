# Core interfaces

The playback session in `core` talks to the rest of the client only through
the interfaces below. They are plain C++20 and do not depend on Qt, so the
session runs unchanged in the app and in the deterministic simulator.

| Header | Interface | Implemented by |
| --- | --- | --- |
| `MediaBackend.h` | `MediaBackend`: load, start, seek, pause, rate, tracks, stop | `media-backend` (mpv), tests (fake) |
| `Timers.h` | `Clock`, `Timers` | `app` (monotonic clock, `QTimer`), simulator |
| `SyncPlay.h` | `SyncPlayOutbox` and the typed group messages | `syncplay` |
| `SessionSnapshot.h` | `SessionObserver`, `SessionSnapshot`, notices | `session::SessionHub`, which hands them to `ui`, `integrations`, `diagnostics` |
| `SessionInput.h` | `SessionInput`: everything the session reacts to | posted by all of the above |

Only the "Ядро и SyncPlay" workstream changes these headers. Other modules
ask for changes through the project coordinator.

## Threading

`PlaybackSession` runs on the Qt main thread. Adapters in `app` move mpv
events and network replies onto that thread with queued connections and post
them as `SessionInput`. Inputs posted while the session is handling another
input are queued and handled afterwards, in order.

## Epochs

- `MediaEpoch` is issued by the session for every `MediaBackend::load`. The
  backend tags every event with the epoch of the file that produced it.
- `RequestId` is issued by the session for each seek and external track.
- `GroupEpoch` is issued by the `syncplay` module on join, create and
  reconnect, and carried by every group message and outgoing message. The
  session adopts the epoch of the latest `group::Joined`.
- `TimerId` serials are issued by the session and never reused.

Inputs from an earlier epoch are dropped on entry to the session, in one place.

## Loading a file

1. The app resolves an item through `PlaybackInfo` and posts `intent::Open`
   with the URL, the `StreamTimeline` and the start position.
2. The session calls `MediaBackend::load(epoch, …)`. The backend opens the file
   and holds it before playback (mpv `on_preloaded` hook).
3. The backend posts `backend::SourceOpened` with `demuxer-start-time`.
4. The session configures the timeline mapping and calls
   `MediaBackend::start(epoch, position)` with the start position in backend time.
5. The backend posts `backend::PlaybackRestarted` with a fresh sample. Readiness
   is confirmed only from that sample, never from `SourceOpened` or the load reply.

## Tracks

`intent::SelectTrack` and `intent::AddExternalTrack` carry the `MediaEpoch`
of the file they are meant for (`SessionSnapshot::media`) and are dropped like
stale backend events once that file is replaced. Track ids are the backend's
(mpv `aid`/`sid`). An external track asked for before the file opened is added
once it has. `SessionSnapshot::externalTracks` lists each one in the order
added, with its status (`Adding`, `Added` with the backend id, `Failed` with
the error); the app finds its own by URL. Switching tracks is local: the group
is not told, and whatever mpv does after it (a refresh seek, a short wait for
data) reaches the session as ordinary backend events.

## Media-session commands

SMTC, MPRIS and Discord post `intent::Play`, `Pause`, `Seek`, `Next` and
`Previous`. In a group they ask the group (`Next` and `Previous` become
`SyncPlay/NextItem` and `PreviousItem` for the current item, so a repeated
press skips once); outside one, `Next` and `Previous` come back as
`notice::QueueStepRequested` for the app's own queue.

## Timeline mapping

| `StreamTimeline::Kind` | Used for | media position |
| --- | --- | --- |
| `Relative` | direct play, HLS, non-HLS transcode (offset = `TranscodingOffsetTicks`) | backend + offset |
| `PreservedVideo` | video with original timestamps | backend |
| `PreservedMusic` | music with original timestamps, `rebaseStartTime` | backend + source start |

A missing or non-finite source start fails a preserved timeline visibly.

## SyncPlay

Incoming: `group::Joined` (with the group name and participants),
`ParticipantsChanged`, `Left`, `ConnectionLost`, `StateChanged`, `Command`,
`QueueChanged`, `RateChangeRejected`, `ClockSynced`.

Participants are user names, one entry per session, from `GroupJoined`,
`UserJoined` and `UserLeft`. SyncPlay 12.2 does not say which participant is
ready or buffering; the group state `Waiting` is the only sign of it.

Outgoing (`SyncPlayOutbox::send`): `Ready`, `Buffering`, `RequestPause`,
`RequestUnpause`, `RequestSeek`, `RequestPlaybackRate`, `RequestStop`,
`RequestNextItem`, `RequestPreviousItem`.

Time: `When`, `EmittedAt` and `LastUpdate` are `ServerInstant`; the session
converts them with the latest `ClockOffset` from `TimeSync`. Media time elapsed
over a wall interval is `round(wallTicks × baseRate)` (`mediaElapsed`); the
wall time to cover a media distance is `round(mediaTicks ÷ baseRate)`
(`wallElapsed`). Absolute instants are never scaled by a rate.

## Playback session

`PlaybackSession` (`PlaybackSession.h`) takes the five ports in
`SessionPorts` and handles one `SessionInput` at a time.

- **Readiness** (`Readiness.h`, ported from the 2.x `PlaybackReadiness`): a load
  or seek is ready only on a `PlaybackRestarted` after the latest request, after
  the backend reported that seek, with a fresh position, and while paused
  within 500 ms of the target; while playing, not more than 500 ms before it.
- **Ready and Buffering** are sent only after confirmed readiness: when a group
  item finishes loading (paused, `IsPlaying=false`), when a group `Seek` or rate
  change completes, on rejoin with the item already open, and when buffering
  starts and ends while the group plays, also when it ends during the seek
  that answers our Buffering. A playing position is moved to the report's
  instant.
- **Commands** run at their `When`, converted with the latest `ClockOffset`.
  A late `Unpause` joins at the projected group position. A repeated command is
  re-applied only when the local state no longer matches it.
- **Drift** (`Reconciler.h`) is a pure function of the last `Unpause`, the
  sample's server instant and the sample's position. Defaults follow Jellyfin
  Web: speed correction between 60 ms and 3 s over 1 s, relative to the group
  base rate and clamped to 0.2–5×; a skip from 400 ms, or when the correction
  would take longer than 5 s. Correction pauses for 1.5 s after a command or skip.
- **Rates**: effective = group base rate, or the temporary correction. Outside a
  group the personal rate applies. Leaving cancels every group timer before the
  personal rate is restored.

## Subscribers

Core has one `SessionObserver`. `jfd::session::SessionHub` (`src/session`) is
that observer in the app and the agent; the UI, the media integrations and
diagnostics subscribe to it. A new subscriber gets the latest snapshot at once.

## Simulator

`tests/sim` runs real `PlaybackSession`s against a modelled mpv (latencies,
keyframe restarts, buffering, a slightly fast or slow clock), a modelled
network (ordered WebSocket, reordering HTTP) and a modelled SyncPlay server
(waiting, buffering, seek, rate, 409 for unsupported rates, join while
playing, next item, participants). External audio is added and tracks are
switched while the group plays; mpv answers with a refresh seek, a short wait
for data or nothing. Seeds are portable.

Invariants checked on every input:

- a stale input (earlier file, earlier group, cancelled timer) changes nothing
  and issues no command or message;
- every backend command is for the current file;
- every Ready names the loaded queue item and a position within 500 ms of
  where the backend really is;
- outside a group the backend rate is always the personal rate.

`ctest` runs 10,000 chaos scenarios, 30-minute drift runs at 1×, 1.25× and
1.5× with a cold second client, a 10-minute drift run in which every client
adds external audio while the group plays, and a stock-server run. Three mutants of
`core` (no epoch check, Ready from an earlier restart, a correction timer that
outlives its group) must each be caught.

```sh
build/tests/jfd_simulator --mode chaos --scenarios 1000 --seed 1
build/tests/jfd_simulator --mode drift --duration 1830 --rate 1.25
build/tests/jfd_simulator --scenarios 1 --seed 2919 --trace 0   # one client's inputs
```
