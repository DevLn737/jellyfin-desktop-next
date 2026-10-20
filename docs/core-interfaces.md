# Core interfaces

The playback session in `core` talks to the rest of the client only through
the interfaces below. They are plain C++20 and do not depend on Qt, so the
session runs unchanged in the app and in the deterministic simulator.

| Header | Interface | Implemented by |
| --- | --- | --- |
| `MediaBackend.h` | `MediaBackend`: load, start, seek, pause, rate, tracks, stop | `media-backend` (mpv), tests (fake) |
| `Timers.h` | `Clock`, `Timers` | `app` (monotonic clock, `QTimer`), simulator |
| `SyncPlay.h` | `SyncPlayOutbox` and the typed group messages | `syncplay` |
| `SessionSnapshot.h` | `SessionObserver`, `SessionSnapshot`, notices | `ui`, `integrations`, `diagnostics` |
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

## Timeline mapping

| `StreamTimeline::Kind` | Used for | media position |
| --- | --- | --- |
| `Relative` | direct play, HLS, non-HLS transcode (offset = `TranscodingOffsetTicks`) | backend + offset |
| `PreservedVideo` | video with original timestamps | backend |
| `PreservedMusic` | music with original timestamps, `rebaseStartTime` | backend + source start |

A missing or non-finite source start fails a preserved timeline visibly.

## SyncPlay

Incoming: `group::Joined`, `Left`, `ConnectionLost`, `StateChanged`,
`Command`, `QueueChanged`, `RateChangeRejected`, `ClockSynced`.

Outgoing (`SyncPlayOutbox::send`): `Ready`, `Buffering`, `RequestPause`,
`RequestUnpause`, `RequestSeek`, `RequestPlaybackRate`, `RequestStop`.

Time: `When`, `EmittedAt` and `LastUpdate` are `ServerInstant`; the session
converts them with the latest `ClockOffset` from `TimeSync`. Media time elapsed
over a wall interval is `round(wallTicks × baseRate)` (`mediaElapsed`); the
wall time to cover a media distance is `round(mediaTicks ÷ baseRate)`
(`wallElapsed`). Absolute instants are never scaled by a rate.
