# Offline downloads

Module `offline` (`jfd::offline`, Qt Core and Network, no screens), stage 3
package 3.10a. Plan: `stage3-briefs-3.9-3.10.md`, section 3.10, with the
changes of its review (3.10 split into 3.10a, originals, and 3.10b,
compressed copies).

Step 1 gave the download folder, manifests, the catalog, the space limits
and the queue. Step 2 adds playing downloads without the server (the
launcher's `LocalSource`), the subtitles, images, lyrics and segments of a
download, the offline watch journal and its sync, and the app's wiring
(`src/desktop/DownloadsInstall.cpp`). The screens ("Download" buttons, the
downloads page, the offline home) follow in a later step.

| Header | What it gives |
| --- | --- |
| `DownloadStore.h` | Folder layout, manifests written atomically, scan, delete, long Windows paths for mpv |
| `Manifest.h` | The `item.json` format and the secret scrub |
| `OfflineCatalog.h` | The in-memory index of all manifests, built at start-up |
| `StorageGuard.h` | Space limit and free-space reserve |
| `DownloadWorker.h` | One thread with its own `QNetworkAccessManager`: Range / If-Range, writing `.part`, size check, rename |
| `DownloadQueue.h` | Order, pause, resume, cancel, retry, reorder, retries with backoff, restart |
| `DownloadSettings.h` | The "Downloads" settings section |
| `ExtrasDownloader.h` | Subtitles, images, lyrics and segments of a finished item |
| `OfflineLocalSource.h` | `session::LocalSource` on the catalog: what the launcher plays from disk |
| `ViewJournal.h` | The offline watch journal (JSON lines), `classifyView` |
| `JournalReportSink.h` | Playback reports with the journal behind them |
| `OfflineSync.h` | Sending the journal, refreshing the manifests from the server |
| `jellyfin-api/Downloads.h` | Original, subtitle, image, lyrics and segment URLs; default quality; which sources have an original; own-server check; UserData get and set; items by id |
| `session/LocalSource.h` | The launcher's hook for local copies (`src/session` does not depend on `src/offline`) |

## Folder layout

```
<download folder>/<serverId>/<userId>/<itemId>/item.json
                                              /media.part     while downloading
                                              /media.<ext>    once complete
                                              /subs/<index>.<srt|ass|ssa|vtt>
                                              /images/<Primary|Backdrop|Logo|Thumb>
                                              /lyrics.json    a track's lyrics
                                              /segments.json  a video's intros and credits
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

Step 2 adds, all optional (older manifests read as before):
`subtitleFiles` (stream index -> file), `imageFiles` (type -> file),
`lyricsFile`, `segmentsFile`, `extrasComplete`, `removedOnServer`,
`changedOnServer` and `refreshedAt`. File names must stay inside the item
folder (no `..`, no absolute paths); others are ignored.

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

## `Items/{id}/Download` on Jellyfin 12.2

Checked against the test stand (`scripts/test-server/stock-server.sh`,
image `jellyfin/jellyfin:12.2.20261005-225228`) on 2026-11-14, and kept as
a server test (`jfd_offline_server_tests`):

| Request | Answer |
| --- | --- |
| `GET` with the session's `Authorization` | 200, `Content-Length` = `MediaSourceInfo.Size`, `Accept-Ranges: bytes`, `Last-Modified`, `Content-Disposition: attachment` |
| `HEAD` | 405 (`Allow: GET`): the size comes from `MediaSourceInfo.Size` or the GET's headers |
| `Range: bytes=1048576-` | 206, `Content-Range: bytes 1048576-<size-1>/<size>`, the rest of the file |
| `Range` + `If-Range: <the Last-Modified>` | 206 |
| `Range` + `If-Range: <an older date>` | 200 with the whole file (the downloader starts over) |
| `Range: bytes=<size>-` | 416, `Content-Range: bytes */<size>` |
| No `Authorization` | 401 |
| User without `EnableContentDownloading` | 403 (`QueueProblem::Refused`) |

There is **no `ETag`**: the resume validator is always `Last-Modified`
(the file's modification time). An `If-Range` with an entity tag is ignored
by the server (it answers 206 whatever the tag), so the downloader never
sends one it did not get from the server. A `.part` of the stand's movie
resumed from 1 MiB, and the result matched the file byte for byte.

## Playing a download

`PlaybackLauncher` asks its `session::LocalSource` (in the app,
`OfflineLocalSource`) before the server. A complete copy of the signed-in
user's item opens without any request:

- the item and media source come from the manifest; `DeliveryMethod` and
  `DeliveryUrl` of embedded streams are cleared (the file has every track,
  whatever a PlaybackInfo answer said about extracting them);
- the stream is `DirectPlay` of the file, no `PlaySessionId`;
- mpv gets a **file system path** (`playback::playerAddress`), never a
  percent-encoded `file://` URL; on Windows a path of 260 characters or more
  gets `\\?\` (`\\?\UNC\` for a share);
- external subtitles are the files in `subs/` (added with `sub-add` by path);
  embedded tracks and the fonts of embedded ASS subtitles are in the file;
- skip segments come from `segments.json`, lyrics from `lyrics.json`
  (`MusicLyrics` asks the copy first).

When: always while the session socket is not `Ready`; while it is, only
with "Play downloads from disk" (`downloads/preferLocal`, on by default). A
request for another version of the item (`mediaSourceId`) goes to the
server. In a group (`playlistItem` set) only an original copy plays from
disk: it has the timeline the others stream. Other users' downloads are not
found.

## Subtitles, images, lyrics, segments

When a download completes, `ExtrasDownloader` fetches, one at a time, from
the server's own address only (no redirects followed) and with the
session's header:

- external text subtitles (`subrip`, `ass`, `ssa`, `webvtt`) by index:
  `Videos/{id}/{sourceId}/Subtitles/{index}/Stream.{format}`; picture
  subtitles (PGS, VobSub) are not fetched;
- `Primary` (600 px wide), `Logo` and `Thumb` (800 px), the first `Backdrop`
  (1920 px), as the server scales them; images of parents (series, album)
  come with the offline screens;
- `Audio/{id}/Lyrics` for tracks (unless `HasLyrics` is false);
- `MediaSegments/{id}` for videos.

404 means the server has none. A network error, 401 or 5xx leaves
`extrasComplete` false: the rest is fetched at the next sign-in or
reconnection. The media plays meanwhile.

## The offline watch journal

`JournalReportSink` is the playback report sink of the player and the music
player (passed to their constructors). While the server can be reached,
reports go out as before (`Sessions/Playing*`; a local copy reports
`DirectPlay` without `PlaySessionId`) and only a report that fails is
written to the journal; a later report of the same playback that succeeds
removes those. Without the server every report goes to the journal.
Live TV is never journaled.

The journal is one file of JSON lines, `offline-journal.jsonl` in the app's
data folder (`AppLocalDataLocation`; the smoke test's profile has its own):

- `event`: server id, user id, item id, `playId` (one playback), position,
  `stopped`, `atMs` and `clockOffsetMs`;
- `update`: the `UpdateUserItemDataDto` computed for an item, and its time;
- `clock`: the last known clock offset.

**Time.** `atMs` is the server's time: the system clock plus the last known
offset (server minus system), from SyncPlay's `TimeSync`
(`GroupClient::clockOffset`, measured whenever the session socket is up).
The offset is stored with each event and in the file, so events recorded
after a restart without the server still use it.

**Sending** (`OfflineSync`, when the session socket becomes `Ready` and
every 30 minutes while it is):

1. `GET Items?ids=...` (batches of 100) for the journaled items: run time,
   `UserData`. An item the server no longer has is dropped from the journal.
2. Per item, the last report of each playback counts by Jellyfin's default
   rules (`classifyView`): under 5 % nothing; over 90 %, or past 5 % of an
   item shorter than 300 s, played; otherwise the position. An
   administrator may have changed these; a user without admin rights cannot
   read them, so playbacks journaled offline follow the defaults.
3. **The server wins** when its `LastPlayedDate` is later than the latest
   journaled playback (the item was watched on another device since): the
   journal drops them and the manifest takes the server's user data.
4. Otherwise the target state is computed **once** and written to the
   journal before it is sent: `PlaybackPositionTicks`, `LastPlayedDate` (the
   event's server time) and, if a playback finished, `Played: true` and
   `PlayCount` = the server's count plus the finished playbacks.
   `POST UserItems/{id}/UserData` stores these as values: checked on 12.2,
   the server keeps the `LastPlayedDate` it is sent and sets `PlayCount` to
   the value (sending the same body twice changes nothing), while
   `POST UserPlayedItems/{id}` adds one each time, so it is not used. After
   a lost answer the next sync finds the server's date equal to the
   update's (not newer) and sends the same values again.
5. An entry goes only after a 2xx; a failed write stays for the next sync.

**Server to copy.** After the journal, the manifests of the user's complete
downloads take the server's `UserData` (watched marks and positions set on
other devices show offline), `removedOnServer` when the server no longer
has the item (the copy plays until the user deletes it), and
`changedOnServer` when the media source's `ETag` differs (downloading it
again is up to the user). Only changed manifests are written.

Not synchronised: favourites, ratings, playlists, and the downloads
themselves between devices.

## In the app

`installDownloads` (after `installSession`, before the player and music
models) builds the services on the "Download folder" setting, loads the
catalog, sets the launcher's local source, and follows the sign-in (the
signed-in user's queue, extras and copies; signing out keeps the downloads,
hidden), the session socket (online state; on `Ready`: sync, missing
extras) and the settings (limit, reserve, "Play downloads from disk"). The
settings section "Downloads" is registered. Nothing in the interface
enqueues downloads yet: the buttons and pages come with the screens.
