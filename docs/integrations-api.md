# Commands from other programs

The running app takes commands from a second start of itself, for
scripts, desktop shortcuts and hotkeys:

```sh
jellyfin-desktop --command play-pause
```

| Command | What it does |
| --- | --- |
| `play-pause` | Pauses what plays, or resumes it |
| `next` | Next track or episode (in a group: asks the group) |
| `previous` | Previous track or episode (in a group: asks the group) |
| `stop` | Stops playback |
| `seek:+N`, `seek:-N` | Seeks N seconds forward or back (decimals allowed) |
| `seek:N` | Seeks to N seconds from the start |
| `fullscreen` | Turns fullscreen on or off |

The commands act the way the system media controls (SMTC, MPRIS) do: in a
watch-together group, play, pause and seek become requests to the group.
The window is not brought forward.

Exit codes of the second start: 0 when the running app received the command,
1 when the app is not running, 2 for a command it does not know (with the
list on standard error). The second start sends the command over the app's
single-instance socket (`platform::SingleInstance`), which only the same user
can open; nothing listens on the network.

`tv` (TV mode) joins the list with stage 3, package 3.3. A local WebSocket
interface is not planned for now.

Implementation: `jfd::remote::parseLocalCommand` and `RemoteControl::run`
(`src/remote`), `runCommand` in `src/desktop/RemoteInstall.cpp`; tests in
`tests/remote`.
