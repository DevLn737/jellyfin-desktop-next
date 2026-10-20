`syncplay-source-derived.json` is copied unchanged from
[DevLn737/jellyfin-desktop](https://github.com/DevLn737/jellyfin-desktop/blob/master/contracts/fixtures/syncplay-source-derived.json)
(contract revision `desktop-handoff-12.2-syncplay.1-r1`, server `494362e`,
web `b2a97d4`). The messages are derived from source, not captured from a live
server.

`recorded/` holds what our client actually received from the Docker test
servers during the drift checks (`tools/syncplay-agent/run.mjs` writes a
`recordings/` folder next to the report; the Drift workflow uploads it). File
names start with the server: `stock-` is the official 12.2 image, `fork-` the
SyncPlay fork. Each event has `ms` since the scenario started and one of
`message` (a session message as sent on the WebSocket), `request` (a SyncPlay
HTTP request and its status) or `connection` (`ready`, `disconnected`,
`resumed` after a sleep). `RecordedTest.cpp` decodes every message and
replays each sequence through the group client.

What the recordings show about 12.2, and how the simulator models it:

- Joining a group that has no queue: `GroupJoined` (State `Idle`), then a
  `Stop` command. The simulator's groups always have an item, so it does not
  model this.
- Joining (or rejoining after a reconnect) a playing group: `GroupJoined`
  (State `Playing`), `PlayQueue` (`NewPlaylist`), then `Seek` to the group's
  position with `StateUpdate` `Waiting`, and `Unpause` once everyone is ready.
  The simulator sends the joiner the same `Seek`.
- `Pause` while playing: `Pause` command and `StateUpdate` `Paused`; `Seek`
  while paused: `Seek` and `Waiting`, then `Pause` and `Paused` (reason
  `Ready`) once everyone reported ready; `Unpause`: `Unpause` and `Playing`.
- A rate request on stock 12.2 answers HTTP 405 (the endpoint does not exist).
- A lost network gives one `disconnected` per failed reconnect attempt; the
  group client treats repeats as one loss.
