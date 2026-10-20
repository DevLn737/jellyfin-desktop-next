# ADR 0003: Playback session and SyncPlay model

Status: accepted, 2026-10-20. Details are refined by the phase 1 prototype.

## Context

In the current client, stale-event protection exists separately in
`PlaybackReadiness`, `PlaybackTimeline`, `ExternalAudioRequest` (C++) and
`window._jmpNativeRequestId` (injected JavaScript). The transcoding offset lives
in JavaScript and music timestamp rebasing lives in C++. SyncPlay timing is
Jellyfin Web's `PlaybackCore.js`. Each of these was correct in isolation, but
together they had no single owner.

## Decision

### One owner

A single-threaded `PlaybackSession` processes one queue of inputs: user
intents, SyncPlay server messages, backend events, timer expirations and
connection changes. Only the session issues backend commands or changes
playback state.

### Epochs

- `MediaEpoch` advances on every file load. Backend events and replies carry it.
- `RequestId` identifies a seek, rate change or external-audio addition inside an epoch.
- `GroupEpoch` advances on group join, leave and reconnect. Correction timers carry it.

Inputs from an earlier epoch are dropped in one place, on entry to the session.

### Time

Distinct types with explicit conversions only:

- `MediaTicks`: logical position on Jellyfin's original timeline (10,000 ticks per millisecond);
- backend time: what mpv reports after any rebasing, converted per epoch;
- local monotonic instants;
- server instants (`When`, `EmittedAt`), converted through the `TimeSync` offset estimate.

The transcoding offset and music start rebasing live together in one per-epoch timeline mapping.

### Desired and actual state

- The group provides the desired state: Idle, Waiting, Paused or Playing; playlist item; position at a server instant; base rate.
- The backend reports the actual state: position, paused, buffering, ready.
- The `Reconciler` is a pure function of (desired, actual, now) that returns an
  action: wait, seek, temporary rate correction, skip, or report Ready/Buffering.
  Initial thresholds come from Jellyfin Web: speed correction 60–3000 ms over
  1000 ms, skip above 400 ms. They are measured, not assumed.
- Effective rate = group base rate × temporary correction. A personal rate
  exists only outside a group and is restored after leaving, once correction timers are cancelled.
- Audio, subtitle and external-track choices are local to each participant and keyed by
  (`MediaSourceId`, `AudioStreamIndex`).
- Ready is reported only after the backend confirms a restart in the current
  epoch with a fresh position near the target.

### Compatibility

Mixed groups with Jellyfin Web, Android and the 2.x desktop client must work.
The playback-rate extension (`POST SyncPlay/SetPlaybackRate`,
`SupportsSyncPlayPlaybackRate`, HTTP 409 for unsupported groups) follows the
contract in `jellyfin-syncplay-speed` and the server fork.

### Verification

- A deterministic simulator with fake clock, backend and server runs thousands
  of randomized, reordered event sequences. Invariants: a stale input never
  changes state, and Ready never carries a previous request's position.
- The session input journal in diagnostics replays real incidents in the simulator.
- Drift (logical position against the group's expected position) is a built-in metric.
