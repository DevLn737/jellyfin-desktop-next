# Test server

`stock-server.sh up` starts a throwaway stock Jellyfin 12.2 in Docker on
port 18096, generates test media with the image's ffmpeg (a 30-second movie
with two audio tracks, an embedded ASS and an external SRT subtitle, and a
series with two 400-second episodes), completes the startup wizard, adds the
libraries and prints:

```
JFD_TEST_SERVER_URL=http://127.0.0.1:18096
JFD_TEST_USER=jfd-test
JFD_TEST_PASSWORD=jfd-test-password
```

With those in the environment, `jfd_server_tests` (configure with
`-DJFD_SERVER_TESTS=ON`) runs sign-in, Quick Connect, search, PlaybackInfo
and stream URLs, playback reports and SyncPlay against it. `stock-server.sh
down` removes the container.

For a server with the SyncPlay playback-rate extension, set `JFD_TEST_IMAGE`
to the fork image and `JFD_TEST_SERVER_FORK=1`; the playback-rate test then
expects the extension's 409 answers instead of a missing route. The fork image
is the SyncPlay harness's (`tools/syncplay-harness`): `PAYLOAD=release
tools/syncplay-harness/scripts/server-up.sh` builds it as
`jellyfin-syncplay-test:local`, and the API workflow builds it the same way.

`extras-server.sh up` starts a second server on port 18097 for the player
checks that would change the first one's item and track counts: a 60-second
movie with an external audio file (`.ru.mka`) and media segments (intro 2-12
s, credits 50-58 s). Stock Jellyfin has no segment provider, so the script
builds a small test plugin (`segments-plugin/`, segments from a
`<video>.segments.json` beside the file) against the image's own assemblies in
the .NET SDK container, installs it before the first start and runs the
segment extraction task. It prints `JFD_EXTRAS_SERVER_URL`, `JFD_EXTRAS_USER`
and `JFD_EXTRAS_PASSWORD`; `jfd_ui_player_server_tests` runs its extras cases
when they are set and skips them otherwise.

Never point these tests at a real server: they create groups, change watched
state and sign sessions out.
