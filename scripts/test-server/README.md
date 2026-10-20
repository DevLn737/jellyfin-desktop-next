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
expects the extension's 409 answers instead of a missing route.

Never point these tests at a real server: they create groups, change watched
state and sign sessions out.
