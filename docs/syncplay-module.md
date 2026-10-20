# SyncPlay module

`src/syncplay` connects the playback session to a Jellyfin SyncPlay group. It
has no Qt dependency; the HTTP and WebSocket connections belong to
`jellyfin-api`, which implements `jfd::syncplay::Transport` and forwards
WebSocket text to `GroupClient::onMessage`.

| Part | Does |
| --- | --- |
| `Protocol.h` | Decodes `SyncPlayCommand` and `SyncPlayGroupUpdate` (GroupJoined, GroupLeft, StateUpdate, PlayQueue, refusals); builds request bodies for Ready, Buffering, Pause, Unpause, Seek, Stop, SetPlaybackRate, New, Join, Leave, Ping; parses `GetUtcTime`. Server times keep all seven fraction digits. |
| `TimeSync.h` | Server clock offset from `GET /GetUtcTime`, as Jellyfin Web: lowest round trip of the last eight, 1 s polls three times, then every 60 s. |
| `GroupClient.h` | Issues a new `GroupEpoch` on every `GroupJoined`, tags all group inputs with it, drops outgoing messages of an earlier membership, reports 409 on `SetPlaybackRate` as `RateChangeRejected`. |

## Rules taken from the 12.2-syncplay.1 contract

- A missing, null or out-of-range `PlaybackRate` is 1x. A stock server never
  sends one, so its groups run at 1x.
- A command without `When` (an idle `Stop`) applies on receipt; a `Seek`,
  `Pause` or `Unpause` without `PositionTicks` is rejected.
- `PlayQueue` without `LastUpdate` is treated as the newest update.
- Reconnect: `onConnectionLost` reports `ConnectionLost` to the session (which
  cancels corrections and keeps playing). After `jellyfin-api` has a fresh
  session and registered capabilities, `onConnectionRestored` rejoins the
  group explicitly. If the group is gone (`GroupDoesNotExist`, `NotInGroup`,
  `JoinGroupDenied`), the session gets `Left`.

## Wiring in the app

```cpp
GroupClient group(transport, clock, [&](SessionInput input) { session.post(input); });
PlaybackSession session({backend, clock, timers, group, observer});
// QTimer: group.pollTime() every group.nextPollDelay().
// WebSocket text message: group.onMessage(text).
```

Messages are copied from the contract fixtures of the 2.x client
(`tests/syncplay/fixtures`). Sequences recorded from the Docker test server
will be added there and replayed through the simulator.
