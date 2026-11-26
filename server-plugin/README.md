# Room Members: a Jellyfin server plugin

Stock Jellyfin tells the members of a SyncPlay group ("room") only each
other's user names. This plugin adds one read-only route, `GET /RoomMembers`,
that answers the caller with the members of the caller's own group: for each
session its user and device name, the ping it last reported to the group
(`SyncPlay/Ping`), whether the group waits for it, and the position it last
reported (`Sessions/Playing/Progress`). Jellyfin Desktop Next shows this in the
room page and in the player's statistics; without the plugin it shows names
only, as before.

- A caller who is in no group gets an empty answer; nobody sees a group they
  are not in.
- Nothing is changed on the server and nothing is stored. The plugin reads the
  server's SyncPlay groups through reflection (they are not exposed to
  plugins); on a Jellyfin version where that fails it logs one warning and
  answers as if nobody were in a group.
- Works with members on any client (Jellyfin Web too): positions come from the
  progress every client reports anyway, so they are a few seconds coarse
  between reports; the app moves them on while the member plays.

Built against Jellyfin 12.2 (`Jellyfin.Controller` 12.2.0, .NET 10).

## Build and test

```
dotnet publish server-plugin/Jellyfin.Plugin.RoomMembers -c Release -o server-plugin/out
server-plugin/smoke-test.sh server-plugin/out   # needs Docker; throwaway stock 12.2 server
```

CI (`.github/workflows/server-plugin.yml`) does both and keeps the DLL as the
`room-members-plugin` artifact.

## Install

1. Stop Jellyfin.
2. In the server's configuration folder, create `plugins/RoomMembers_1.0.0.0/`
   and put `Jellyfin.Plugin.RoomMembers.dll` in it. The configuration folder is
   `/config` in the official Docker image, `/var/lib/jellyfin` for the Linux
   packages and `C:\ProgramData\Jellyfin\Server` on Windows.
3. Start Jellyfin. Dashboard -> Plugins lists "Room Members" as Active.

To remove it, delete that folder and restart Jellyfin.

## Answer

```json
{
  "GroupId": "8c091b8782874d2591534f92ab393375",
  "ServerTimeUtc": "2026-11-26T09:30:50.9851686Z",
  "Members": [
    { "UserName": "bob", "DeviceName": "bob-pc", "Client": "Jellyfin Web", "IsSelf": false,
      "PingMs": 137, "IsBuffering": false, "ItemId": "dfbdd60520686993a1b71d532794ed80",
      "PositionTicks": 1250000000, "IsPaused": true,
      "PositionUpdatedUtc": "2026-11-26T09:30:50.9530588Z" }
  ]
}
```

Null fields are left out (members that play nothing have no `ItemId`,
`PositionTicks` or `PositionUpdatedUtc`).
