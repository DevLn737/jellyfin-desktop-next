# Jellyfin API client

Modules `jellyfin-api` (`jfd::jellyfin_api`), `platform` (credential store
only, so far) and `library` (`jfd::library`). All use Qt Core and Network;
`jellyfin-api` also uses Qt WebSockets. Server timestamps and the monotonic
clock come from `core` (`ServerInstant`, `LocalInstant`, `Clock`).

| Header | What it gives |
| --- | --- |
| `JellyfinClient.h` | One server and user: sign-in (password, Quick Connect), server version check, session capabilities, items, `PlaybackInfo`, playback reports, watched state, raw requests |
| `MusicApi.h` | Music lists, playlists and lyrics (`Audio/{id}/Lyrics`: synced lines with start ticks or plain text; 404 is "no lyrics") |
| `JellyfinClient.h` (library) | Libraries (`UserViews`), "Continue watching" (`UserItems/Resume`), "Next up" (`Shows/NextUp`), latest per library (`Items/Latest`), filtered and sorted pages, filter values (`Items/Filters`), similar items, people (`Persons`), favorites, image URLs, item details with trickplay, media segments, trickplay tile URLs and authorized fetches |
| `ServerDiscovery.h` | Servers on the local network (UDP broadcast to port 7359) |
| `SessionSocket.h` | The `/socket` connection: keep-alives, reconnect with backoff, capabilities re-reported on every connect, raw `SyncPlayCommand` and `SyncPlayGroupUpdate` |
| `SyncPlayApi.h` | SyncPlay routes, including `SetPlaybackRate` and its 409, `Queue` (next or last), and `GetUtcTime` samples |
| `Playback.h` | `PlaybackInfo` to mpv URLs (direct play, static stream, transcode), external subtitles and audio, mpv track ids, report bodies |
| `DeviceProfile.h` | The 2.x client's device profile and its options |
| `ServerTime.h` | ISO 8601 with seven fractional digits to and from `core::ServerInstant` |
| `platform/CredentialStore.h` | Tokens in Windows Credential Manager or the Secret Service |
| `library/ItemBrowser.h` | Search, seasons and episodes as a QML list model (the measurement prototype's) |
| `library/SignIn.h`, `ServerStore.h` | The login flow: servers on the network and saved ones, version check, password and Quick Connect, token in the credential store, restore at start-up, sign-out, expired sessions |
| `library/HomeModel.h` | Home rows: Continue watching, Next up, latest per video library; reloads keep rows on screen |
| `library/LibraryModel.h` | A library (or a collection, or a person's films and series by `personIds`) as a grid with every row from the first page and pages loaded as rows are shown; sort and filters |
| `library/ItemDetails.h` | A movie, series, season or episode page: seasons, episodes, similar, what Play starts, favorite and watched toggles; emits `playRequested` for the session's launcher |
| `library/SearchModel.h` | Search as you type (titles, music with tracks, and people from `Persons`), stale answers dropped |
| `library/ImageCache.h` | Images decoded at card size; memory limit (128 MiB), disk cache in the profile, newest requests first, cancellation |
| `library/LibraryUpdates.h` | `UserDataChanged` and `LibraryChanged` from the socket, and local changes, to every model |

## Conventions

- Images (`Items/{id}/Images/...`) need no authentication; trickplay tiles
  (`Videos/{id}/Trickplay/{width}/{index}.jpg`) do, so they are fetched with
  `authorizedGet`, which sends the token only to this server. A tile holds
  `TileWidth` x `TileHeight` thumbnails row by row; `trickplayFrame()` maps a
  position to the tile and rectangle.
- `mediaSegments` is an empty list on a server without a segment provider
  (a stock server has none); the server tests check that.

- Every call takes a context `QObject*` and a callback with `Result<T>`. The
  callback never runs after the context is destroyed. A 401 on a signed-in
  client clears the session and emits `sessionExpired`.
- Errors keep the HTTP status and the ProblemDetails `code`.
  `ApiError::isPlaybackRateConflict()` is the extension's 409 (on
  `SetPlaybackRate` and on joining an accelerated group);
  `ApiError::Kind::Unsupported` on `SetPlaybackRate` means the server has no
  extension, and the group stays at 1x.
- `probeSyncPlayPlaybackRate` asks the server whether it has the extension
  with an out-of-range rate, which changes nothing on either server.
- Media URLs carry the token as `ApiKey` because mpv cannot be given headers
  for one origin only; `api_key` is never generated. Sources that need extra
  HTTP headers are not played directly.
- `SessionSocket` does not parse SyncPlay messages; the `syncplay` module
  turns them into `core::group` inputs. `connectionGeneration()` increases on
  every connect, so it can tell a reconnect (membership may be lost) from a repeat.
- `JellyfinClient::rawRequest` is the authenticated call for code that builds
  its own requests, such as the `syncplay` group client's `Transport`: it
  passes the status and body of any answer through (the 409 included) and
  reports status 0 when no answer arrived.

## Remote control (`src/remote`)

Module `remote` (`jfd::remote`, Qt Core) reads the session messages other
clients of the same user send through the server ("Play on" in Jellyfin Web
and the phone apps) and carries them out through `RemoteActions`, which
`src/desktop/RemoteInstall.cpp` connects to the player, the music player, the
group model, the navigator and the shell. It has no screens and does not
depend on `ui/*`.

What arrives on `/socket` (`SessionSocket::messageReceived`), and what the
client does with it:

| `MessageType` | `Data` fields | Done here |
| --- | --- | --- |
| `Play` | `ItemIds`, `StartIndex`, `StartPositionTicks`, `PlayCommand` (`PlayNow`, `PlayNext`, `PlayLast`, `PlayShuffle`, `PlayInstantMix`), `MediaSourceId`, `AudioStreamIndex`, `SubtitleStreamIndex`, `ControllingUserId` | Outside a group: tracks go to the music queue (`PlayNext`/`PlayLast` queue them while music plays); anything else plays alone in the player (`PlayNext`/`PlayLast` of a video while one plays are ignored). In a group: `PlayNow`/`PlayShuffle` replace the group's queue (`SyncPlay/SetNewQueue`), `PlayNext`/`PlayLast` add to it (`SyncPlay/Queue`). `PlayInstantMix` is ignored. Media source and stream choices are read but the player chooses its own |
| `Playstate` | `Command` (`Stop`, `Pause`, `Unpause`, `PlayPause`, `NextTrack`, `PreviousTrack`, `Seek`, `Rewind`, `FastForward`), `SeekPositionTicks` | All of them, through `integrations::commandInput` and the session's `post`, as the system media controls do, so in a group they are requests to the group. `Rewind` and `FastForward` step 10 s back and 30 s forward |
| `GeneralCommand` | `Name`, `Arguments` (strings) | Only the names in `SupportedCommands` (below); the parser knows every `GeneralCommandType` and logs the others as ignored |

`SupportedCommands` (`RemoteControl::supportedCommands()`) comes from the
same table that runs the commands, so a name is advertised exactly when its
action is connected: `SetVolume`, `VolumeUp`, `VolumeDown`, `Mute`, `Unmute`,
`ToggleMute`, `SetAudioStreamIndex`, `SetSubtitleStreamIndex`,
`DisplayMessage` (a toast), `ToggleFullscreen`, `GoHome`, `GoToSearch`,
`GoToSettings`, `SetRepeatMode`, `SetShuffleQueue` (music),
`SetMaxStreamingBitrate`. Navigation (`MoveUp` ... `Back`) and `SendString`
are handled by the table too, but are not advertised until the shell's input
router (stage 3, package 3.3) connects them. Never advertised: `SendKey`,
`PageUp`, `PageDown`, letters, `ToggleOsd`, `ToggleContextMenu`,
`TakeScreenshot`, `DisplayContent`, channels and `Guide`, `ToggleStats`,
`PlayMediaSource`, `PlayTrailers`, `SetPlaybackOrder`.

The setting `remote/allowServerControl` (Settings > Remote control, on by
default as in Jellyfin Web) turns it off: the capabilities are reported
again with `SupportsMediaControl: false` and no commands, and messages that
still arrive are ignored. The second start's `--command`
([integrations-api.md](integrations-api.md)) goes through the same
`RemoteControl` and works either way.

Tests: `jfd_remote_tests` (fake server messages, a fake socket server, the
second start over `SingleInstance`); `jfd_remote_server_tests` sends `Play`,
`Playstate`, `GeneralCommand` and `Message` from a second session of the
test user through the 12.2 server and checks each is carried out within 1 s.

Tests: `jfd_api_tests`, `jfd_library_tests`, `jfd_platform_tests` run offline
against an in-process fake server; `jfd_server_tests` runs against Jellyfin
12.2 in Docker (see `scripts/test-server`).
