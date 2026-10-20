# ADR 0002: Module boundaries

Status: accepted, 2026-10-20.

## Decision

Each module is a CMake target.

| Module | Owns | Must not depend on |
| --- | --- | --- |
| `core` | Domain model, time types, epochs, `PlaybackSession`, SyncPlay `Reconciler`, and the interfaces the session uses: `MediaBackend`, `Clock` and timers, outgoing SyncPlay messages, `SessionSnapshot` | Qt, mpv, network, UI |
| `jellyfin-api` | Typed HTTP/WebSocket client, auth, items, `PlaybackInfo`, device profile, playback reporting, server capability discovery | mpv, UI |
| `syncplay` | Group protocol, `TimeSync` (server clock offset), mapping of server messages to session inputs | mpv, UI |
| `media-backend` | mpv implementation of `core::MediaBackend`, fake implementation for tests | network, UI |
| `library` | Requests, cache and QML models for library screens | mpv |
| `platform` | Power/sleep inhibition, media keys and system media controls, taskbar, display refresh switching, single instance, paths and profiles, credential storage (Windows Credential Manager, Keychain, Secret Service), input devices | UI |
| `integrations` | Read-only subscribers to `SessionSnapshot` with a narrow command set; Discord first | session internals |
| `settings` | Schema, versioning, migrations including import from the 2.x client | anything (leaf) |
| `diagnostics` | Bounded local log buffer, session input journal, drift and frame metrics | anything (leaf) |
| `ui` | QML screens and the video item; reads snapshots and `library` models, sends intents | `jellyfin-api` and mpv directly |
| `app` | Wiring, startup, the queue adapter | — |

Dependency graph (an arrow points from a module to what it uses):

```text
app -> ui, integrations, syncplay, media-backend, library, platform
ui -> library, core
library -> jellyfin-api, core
syncplay -> jellyfin-api, core
media-backend -> core
integrations -> core
jellyfin-api -> platform
every module -> settings, diagnostics
```

`core` is plain C++20 so it can be tested on every platform without Qt and
driven by a deterministic simulator.

### Threads

`PlaybackSession` runs on the Qt GUI thread. The queue adapter in `app`
delivers mpv and network events to the session through queued connections;
`core` itself has no Qt dependency. The UI reads immutable `SessionSnapshot`
values.

## Deliberately not built yet

- Dynamically loaded plugins, a stable plugin ABI, sandboxes or scripting.
- More than one video backend. The backend interface exists for testing.
- A separate core process or IPC.
- A dependency-injection framework or a global event bus.
- Theming.

An integration interface becomes public only after two or three real
integrations (for example Discord plus Trakt or Last.fm) have used it unchanged.
