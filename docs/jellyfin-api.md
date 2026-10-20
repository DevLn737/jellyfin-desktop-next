# Jellyfin API client

Modules `jellyfin-api` (`jfd::jellyfin_api`), `platform` (credential store
only, so far) and `library` (`jfd::library`). All use Qt Core and Network;
`jellyfin-api` also uses Qt WebSockets. Server timestamps and the monotonic
clock come from `core` (`ServerInstant`, `LocalInstant`, `Clock`).

| Header | What it gives |
| --- | --- |
| `JellyfinClient.h` | One server and user: sign-in (password, Quick Connect), server version check, session capabilities, items, `PlaybackInfo`, playback reports, watched state, raw requests |
| `SessionSocket.h` | The `/socket` connection: keep-alives, reconnect with backoff, capabilities re-reported on every connect, raw `SyncPlayCommand` and `SyncPlayGroupUpdate` |
| `SyncPlayApi.h` | SyncPlay routes, including `SetPlaybackRate` and its 409, and `GetUtcTime` samples |
| `Playback.h` | `PlaybackInfo` to mpv URLs (direct play, static stream, transcode), external subtitles and audio, mpv track ids, report bodies |
| `DeviceProfile.h` | The 2.x client's device profile and its options |
| `ServerTime.h` | ISO 8601 with seven fractional digits to and from `core::ServerInstant` |
| `platform/CredentialStore.h` | Tokens in Windows Credential Manager or the Secret Service |
| `library/ItemBrowser.h` | Search, seasons and episodes as a QML list model |

## Conventions

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

Tests: `jfd_api_tests`, `jfd_library_tests`, `jfd_platform_tests` run offline
against an in-process fake server; `jfd_server_tests` runs against Jellyfin
12.2 in Docker (see `scripts/test-server`).
