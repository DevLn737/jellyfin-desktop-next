# ADR 0002: Module boundaries

Status: accepted, 2026-10-20.

## Decision

Each module is a CMake target. Dependencies point downwards in this list only.

| Module | Owns | Must not depend on |
| --- | --- | --- |
| `core` | Domain model, time types, epochs, `PlaybackSession`, SyncPlay `Reconciler` | Qt, mpv, network, UI |
| `jellyfin-api` | Typed HTTP/WebSocket client, auth, items, `PlaybackInfo`, device profile, playback reporting, server capability discovery | mpv, UI |
| `syncplay` | Group protocol, `TimeSync` (server clock offset), mapping of server messages to session inputs | mpv, UI |
| `media-backend` | Backend interface (load, seek, rate, tracks, external audio, epoch-tagged events), mpv implementation, fake implementation for tests | network, UI |
| `platform` | Power/sleep inhibition, media keys and system media controls, taskbar, display refresh switching, single instance, paths and profiles, input devices | UI |
| `integrations` | Read-only subscribers to playback events with a narrow command set; Discord first | session internals |
| `settings` | Schema, versioning, migrations including import from the 2.x client | — |
| `diagnostics` | Bounded local log buffer, session input journal, drift and frame metrics | — |
| `ui` | QML screens and the video item; reads state snapshots, sends intents | backend and network directly |
| `app` | Wiring and startup | — |

`core` is plain C++20 so it can be tested on every platform without Qt and
driven by a deterministic simulator.

## Deliberately not built yet

- Dynamically loaded plugins, a stable plugin ABI, sandboxes or scripting.
- More than one video backend. The backend interface exists for testing.
- A separate core process or IPC.
- A dependency-injection framework or a global event bus.
- Theming.

An integration interface becomes public only after two or three real
integrations (for example Discord plus Trakt or Last.fm) have used it unchanged.
