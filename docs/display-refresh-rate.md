# Display refresh rate

Stage 3, package 3.5. A film at 23.976 frames per second judders on a 60 Hz
display (3:2 pulldown). With the setting on, the client switches the display
to a rate the video divides before it opens the file, and puts the original
rate back afterwards.

## Where

| System | Backend | Notes |
|---|---|---|
| Windows | `QueryDisplayConfig` / `SetDisplayConfig` | Fractional rates (23.976, 29.97, 59.94, 119.88) as rationals. If refused, `ChangeDisplaySettingsExW` with `CDS_FULLSCREEN` (whole rates only). |
| Linux, X11 | XRandR (`XRRSetCrtcConfig`) | Same size and position; only the timing changes. Needs libX11 and libXrandr at build time (optional; without them the rate is never switched). |
| Linux, Wayland | none | No protocol lets a client change the mode. XWayland's RandR is emulated and changes nothing, so a session with `WAYLAND_DISPLAY` set, `XDG_SESSION_TYPE=wayland`, or an `XWAYLAND*` output counts as Wayland. There `video-sync=display-resample` (setting) is the only option. |
| Others | none | |

Code: `src/platform/include/jfd/platform/DisplayModes.h` (backends and the
choice), `DisplayModeSwitcher.h` (switch, wait, restore, journal).

## Which mode

`chooseDisplayMode` (tested by the table in `tests/platform/DisplayModesTest.cpp`):

1. Only modes with the current size, never interlaced.
2. The current mode already fits (the video's rate or a whole multiple:
   24 fps at 120 or 144 Hz): no switch.
3. A mode at the video's own rate. "Same" means within 0.05 %, half the
   0.1 % between 24 and 23.976, so 23.976 never plays at 24 Hz and 24 never
   at 23.976.
4. A whole multiple: the smallest of 100 Hz or more (24 -> 120, not 144),
   else the largest below 100 (25 -> 50).
5. Otherwise no switch. 23.976 on a {60, 120, 144, 165} Hz monitor stays as
   it is: 23.976 x 5 = 119.88 is 0.1 % from 120.

The next video starts from the original mode: a video the original fits
goes back to it.

## When (player)

Only with `playback/refreshRateSwitch` on, the window full screen and a
`RealFrameRate` from the server. The switch happens before the session
opens the file; the player waits for the system's notice of the new mode or
`playback/refreshRateSettleMs` (2000 ms, at most 5000), whichever comes
first. In a SyncPlay group the readiness barrier waits for the slowest member
as it does for buffering. Going full screen while a video plays does not
switch (no HDMI resync in the middle of a film).

The original mode comes back when playback stops (from the player, a group
or a remote), the app quits, the window leaves full screen, the video goes
to picture-in-picture or the setting is turned off. The next episode keeps
the switched mode if it fits, or switches from the original one.

The hook is `PlaybackLauncher::OpenGate` (`SessionRuntime::setOpenGate`),
set by `PlayerModel::beforeOpen`; no change to `core`.

`playback/displayResample` sets mpv's `video-sync=display-resample` (off:
`audio`, mpv's default): the fallback, and the only option on Wayland.
2.x settings carry over: `video.refreshrate.auto_switch`,
`video.refreshrate.delay` (seconds, to milliseconds) and `video.sync_mode`
(only `display-resample`).

After the file opens, mpv's `container-fps` is compared with `RealFrameRate`;
a difference over 1 % (interlaced or variable frame rate files) is logged
and nothing is switched again.

## Crashes

`SetDisplayConfig` and XRandR are not undone by the system when the process
ends. Before the first switch the original mode is written to
`display-mode.json` next to the settings file (the profile); it is removed
once the mode is back. A run that crashed or was killed leaves it, and the
next start (`DisplayModeSwitcher::restorePending`) puts that mode back. A
journal written under another session type (X11, then Wayland) or for a
screen that is not connected is dropped.

## What CI checks and what it does not

- The choice table, the switcher with a fake backend (order, settle time,
  restore on every path, the journal after a crash).
- The launcher's gate (`tests/session/OpenGateTest.cpp`) and the player
  model on a fake display through the real launcher
  (`tests/ui/PlayerRefreshRateTest.cpp`): switch, wait, open; restore on
  stop, leaving full screen, picture-in-picture, quitting, a stop from the
  session, the setting turned off, a stop while settling; crash recovery.
- On Xvfb (Linux CI), real XRandR switches: modes added to Xvfb's output,
  23.976 Hz and back, and the restore from a journal.
- On Windows CI, only the listing of the runner's virtual adapter.
- Not in CI: real monitors and TVs, HDMI resync time, Windows'
  `SetDisplayConfig` with a fractional rate. Those are the pre-release
  session rows 10 (Windows: 165 Hz and 60 Hz monitors, files at
  23.976/24/25/50/60) and 11 (Linux X11).
