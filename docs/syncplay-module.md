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

## Rooms on a stock 12.2 server

The rooms tab (`RoomsModel`, `src/session`; screens in `docs/ui.md`) adds
no request of its own beyond `GET SyncPlay/List`, which it polls every 5 s
only while the tab shows in the active window. Joining and creating are the
usual `SyncPlay/Join` and `SyncPlay/New`; both answer 204 and the server
refuses later, over the socket, as a `SyncPlayGroupUpdate`:

| `Type` | Text on the tab |
| --- | --- |
| `JoinGroupDenied` | You may not join this room. |
| `CreateGroupDenied` | You may not create rooms on this server. |
| `GroupDoesNotExist` | This room no longer exists. |
| `LibraryAccessDenied` | This room is playing something from a library you cannot see. |
| `NotInGroup` (while waiting for an answer) | You are not in this room. |
| any other type the client does not know | The server refused. Try again later. (and a warning in the log, `jfd.session.group`) |

`GroupModel::refusalText` holds these; the Russian ones are in
`src/session/i18n/session_ru.ts`. The client checks the user's
`Policy.SyncPlayAccess` before it asks: with `None` it sends no SyncPlay
request at all (the list, join, create, a new queue or items added from the
phone), with `JoinGroups` no `New`.

The member list of `SyncPlay/List` names each user once, so two sessions of
one user in a room count as one member on the card.

