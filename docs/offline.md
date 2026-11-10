# Offline downloads

Module `offline` (`jfd::offline`, Qt Core and Network, no screens), stage 3
package 3.10a. Plan: `stage3-briefs-3.9-3.10.md`, section 3.10, with the
changes of its review (3.10 split into 3.10a, originals, and 3.10b,
compressed copies).

This file covers the first step: the download folder, manifests, the
catalog, the space limits and the queue. Playing without the network, the
offline journal and the screens follow in later steps.

| Header | What it gives |
| --- | --- |
| `DownloadStore.h` | Folder layout, manifests written atomically, scan, delete, long Windows paths for mpv |
| `Manifest.h` | The `item.json` format and the secret scrub |
| `OfflineCatalog.h` | The in-memory index of all manifests, built at start-up |
| `StorageGuard.h` | Space limit and free-space reserve |
| `DownloadWorker.h` | One thread with its own `QNetworkAccessManager`: Range / If-Range, writing `.part`, size check, rename |
| `DownloadQueue.h` | Order, pause, resume, cancel, retry, reorder, retries with backoff, restart |
| `DownloadSettings.h` | The "Downloads" settings section |
| `jellyfin-api/Downloads.h` | Original URL, default quality, which sources have an original, own-server check |

## Folder layout

```
<download folder>/<serverId>/<userId>/<itemId>/item.json
                                              /media.part     while downloading
                                              /media.<ext>    once complete
```

Names are ids (checked to be `[A-Za-z0-9-]`), never titles. The extension
comes from the source's container. An item is complete only when its
manifest says so, and that is written after the file's size was checked and
`media.part` renamed. A process killed between the rename and the manifest
is repaired at the next start (the file is there with the expected size and
no `.part`).

Windows: Qt's own file access handles paths past 260 characters.
`DownloadStore::externalPath` gives mpv the `\\?\` form of such paths.

## Manifest (`item.json`, format 1)

`serverId`, `userId`, `itemId`, `state` (`queued`, `paused`, `failed`,
`complete`), `quality` (`original`), `mediaFile`, `expectedSize`,
`resumeValidator` (the ETag or Last-Modified of the response the `.part`
came from), `queuedAt` (queue order), `completedAt`, and the server's own
JSON: `item` (BaseItemDto), `mediaSource` (MediaSourceInfo) and `userData`.
The offline screens parse those with `api::json`, as online.

Never stored: `TranscodingUrl`, `DirectStreamUrl`, `ApiKey`, `AccessToken`
keys, and `ApiKey` / `api_key` / `X-Emby-Token` query parameters inside any
string. `toJson` removes them whatever the caller passes.

The catalog reads 10,000 manifests (200 videos, 9,800 tracks) in about
0.35 s on a Linux debug build; the threshold is 1 s on CI Linux. SQLite is
not needed.

## Downloading

- `GET Items/{mediaSourceId}/Download` with the session's `Authorization`
  header and `Accept-Encoding: identity`. The token goes only to the server's
  own scheme, host and port; a redirect elsewhere is not followed (the item
  fails), and the token is never part of a URL, a file name or a manifest.
- Resume: `Range: bytes=<size of .part>-` with `If-Range: <validator>`. 206 is
  appended, 200 starts the file over (no range support, or the file
  changed), 416 with a matching size completes it. A weak ETag is not used as
  a validator; Last-Modified is.
- The finished file must have the size the server announced (and
  `MediaSourceInfo.Size`) before it is renamed.
- Retries: 5, after 2, 4, 8, 16 and 32 s; an attempt that received data
  resets the count. 401 stops the queue until the next sign-in; other 4xx
  fail the item at once.
- One transfer at a time by default (1 to 3). Progress reaches the GUI
  thread at most four times a second.
- Restart: unfinished items of the signed-in user continue in their queue
  order. Other users' downloads stay on disk, hidden, and continue when that
  user signs in again. Signing out keeps downloads (default; deleting them is
  a choice in the sign-out dialog, a later step).

## Space

Settings (section "Downloads"): `downloads/folder` (default
`<Videos>/Jellyfin`), `downloads/maxGiB` (100; 0 = no limit),
`downloads/reserveGiB` (5). A download does not start when complete and
running downloads plus this one would pass the limit, or when its remaining
bytes plus 10 % would leave less free space than the reserve. While writing,
free space is checked every 64 MiB; below the reserve the queue stops with
`QueueProblem::LowSpace` until `resumeAll()`.

## Rules fixed for the later steps

From the review of the plan, so the journal and playback steps do not decide
differently:

- Offline viewing events are stored with the server's time: the local time
  corrected by the last known clock offset from SyncPlay's `TimeSync`, and
  the offset itself. "Who is newer" compares that with the server's
  `LastPlayedDate`.
- `PlayCount` is replayed idempotently: the journal records the target
  value at the first attempt (or does not touch it), so a lost answer and a
  second send never count a play twice.
- A local copy played while online reports as usual (`Sessions/Playing*`,
  `PlayMethod = DirectPlay`); the journal is used only when a report fails.
