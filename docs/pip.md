# Picture in picture

Research and design of the PiP window (stage 2c, player package); the
Linux/OpenGL path is implemented and tested, Windows (D3D11) and the
platform rows marked "to verify" are not. "Verified" below means checked in this repository's code;
platform behaviour that I could not run here is marked "to verify" and has
a test on real hardware listed at the end.

## What the user gets

A small frameless window that stays above other windows and shows the
playing video, so the main window can go back to the library (or be
minimised) while watching. The window has pause/play, "back to the big
window" and close. Closing it returns to the big player (the video does not
stop), as the stage 2c table says.

## How the video is drawn now (verified)

- `PlayerModel::attachVideo(host, owner)` makes one video item inside `host`:
  `MpvVideoItem` (OpenGL, a `QQuickFramebufferObject`) or, on Windows with
  Direct3D 11, `MpvD3D11VideoItem`. The item creates mpv's render context on
  its window's scene graph render thread and frees it when the item goes
  away.
- libmpv allows **one render context per mpv handle**. So exactly one video
  item can show the video at a time. A second window cannot get a second
  item on the same player.
- `PlayerPage.qml` calls `attachVideo` once and calls `player.stop()` when it
  is destroyed. The page lives in the main window's `Navigator` stack, so
  today leaving the page stops playback.
- With HDR10 output (Windows, D3D11) the video goes under the window's
  interface layer (`HdrScene`, `HdrOutput.videoLayer`) and the window has an
  HDR swap chain.

## Options considered

1. **Move the video to a second window (chosen).** Create the PiP window
   (`Window` with the flags below) with its own host item, free the video
   item in the main window and call `attachVideo` for the PiP host (and the
   other way on return). The render context is destroyed on the first
   window's render thread and created on the second one's; mpv keeps
   decoding, so playback state, the session, SyncPlay and reports are not
   touched. Expected cost: a few dropped frames at each move (to measure).
2. A second video item with a shared GL context. Not possible: one render
   context per handle (above).
3. A second mpv instance for the small window. Rejected: two playback
   positions to keep in step, double decoding, and the session would have two
   players to report. Not worth it for a small window.
4. Render into a texture and share it with the second window. Needs a
   shared GL context or a shared D3D11 device between two Qt Quick windows
   and a second scene graph reading a texture made in the first. More code
   and more platform risk than option 1 for the same result.

## Measured: moving the video (tests/render/PipMoveTest.cpp)

- Freeing the render context while a file plays **ends the file** ("video
  output initialization failed", end of file). A plain move therefore stops
  playback.
- Switching mpv's video track off (`vid=no`) before the item goes away, and
  back (`vid=auto`) once the new window's context is ready, keeps the file
  and the audio playing; the position goes on across three moves in a row
  (OpenGL, Xvfb/llvmpipe).
- The old window frees its context on its own render thread, so the new one
  may ask first. Both video items now ask again on the next frame (up to 100
  tries) instead of failing for good.
- `PlayerModel` owns this: `enterPip()`/`leavePip()` switch the track off and
  delete the item, `attachVideo` (called by the page or by `PipWindow`) makes
  the new one, `videoReady` switches the track back on. Not done for HDR10
  output: the PiP button is hidden then.

## What has to change besides the window

- The page must not stop playback when it closes for PiP. `Component.onDestruction:
  player.stop()` becomes "stop unless the video moved to PiP". The stop
  belongs to whoever owns the video: the page, or the PiP window.
- Something has to own the PiP window after the page is gone (it must not be
  a child of the page). `PlayerModel` (the `PlayerSession` singleton) is the
  natural owner: `pip.active`, `enterPip()`, `leavePip()`, and a QML
  component `PipWindow.qml` it creates through the engine.
- Back from PiP: `Navigator.navigate("player")` again, then `attachVideo`
  into the page's host.
- Keys in the PiP window: Space pause, Esc or X close (back to the big
  window), a click on the video toggles pause, drag moves the window. Resize
  with the window edges or a corner handle, keeping the video's aspect
  ratio.
- The mini-player strip (`shell.miniPlayerSlot`) is for music and is hidden
  while video plays; PiP is separate from it.
- A SyncPlay group keeps working: the session does not know which window
  shows the video. Pausing in PiP is the same intent as in the big player.

## Platforms

| | Windows | Linux X11 | Linux Wayland |
| --- | --- | --- | --- |
| Always on top | `Qt.WindowStaysOnTopHint` (topmost window style). Works. | The same hint sets `_NET_WM_STATE_ABOVE`. Works in EWMH window managers (to verify on GNOME/Xorg, KDE, Xfce). | **No client request exists** in the core protocol (`xdg-shell`). The hint is ignored by Qt. |
| Start position (a corner) | `setPosition` works. | `setPosition` works. | The client cannot set a position; the compositor places the window. |
| Drag the window | Own handling or `startSystemMove`. | The same. | `QWindow::startSystemMove()` works (it asks the compositor). |
| Resize | `startSystemResize` or edges. | The same. | `startSystemResize()` works. |
| Frameless | `Qt.FramelessWindowHint`. | The same. | Qt draws no decoration for a frameless window; fine. |
| Not in the taskbar | `Qt.Tool`. | `Qt.Tool` (skip taskbar hint). | Compositor-dependent. |
| Video moves between windows | D3D11 item; each Qt Quick window has its own device context and swap chain (to verify). | OpenGL; one context per window (to verify). | OpenGL; the same. |

### Wayland

A Wayland client cannot make itself stay above other windows or choose where
it appears. What is known (to verify per compositor before we promise
anything):

- GNOME (Mutter) and others offer "Always on top" in the window menu or
  keyboard shortcuts, set by the user, per window. Some compositors let
  users write rules (KDE window rules, Sway `for_window` rules) that match
  the title or app id and make a window floating and on top; that is user
  setup, not something the app can do.
- `wlr-layer-shell` can place a surface in the overlay layer, but it is
  offered by wlroots-based compositors and KDE (not GNOME) and is meant for
  shell components, not normal windows. Using it for a video window would be
  a special build of the window with its own limits; not planned.
- Qt's `Qt.WindowStaysOnTopHint` is only a request; on Wayland it has no
  protocol to carry it.

Proposed behaviour on Wayland: open the same small frameless window
(sensible default size, our own minimal controls, drag with
`startSystemMove`), **without** promising that it stays on top, and show a
one-line hint the first time ("Your desktop does not let applications keep a
window on top. Use the window menu's Always on top."), with the app id and
the window title fixed (`Jellyfin Desktop Next - Picture in picture`) so
users can write a compositor rule. If a compositor refuses or the window
does not behave, the PiP button hides itself (feature detection by platform
name: `QGuiApplication::platformName() == "wayland"`).

## HDR

With HDR10 output the main window has an HDR swap chain and the video lives
under the interface layer. A second window would get its own swap chain.
Proposed first version: entering PiP while HDR output is active tone-maps
(SDR video item in the small window; HDR is for the big window), or the
button is hidden while `HdrOutput.active`. To decide with a test on an HDR
monitor, which is on Ruslan's deferred measurement list.

## Test plan

- Automated (Linux, offscreen/Xvfb, headless mpv as in
  `jfd_ui_player_server_tests`): enter PiP, the PiP window exists with the
  expected flags, playback continues across the move (position keeps
  advancing, the server session does not report a stop), leaving PiP shows
  the page again, closing the PiP window returns to the page, closing
  during a SyncPlay group keeps the group.
- Automated (Windows CI, `App (windows-2022)`): the same with the D3D11
  item; the topmost style is set on the native window.
- By hand on real hardware (deferred list): always-on-top on Windows, GNOME
  Xorg, KDE Xorg, KDE Wayland, GNOME Wayland; frame drops at each move;
  HDR monitor; two monitors.

## Decision for the owner

Support: Windows and Linux X11 fully (always on top, corner placement);
Wayland as the reduced mode above. The cost of the Wayland part is small
(the same window without the on-top guarantee plus one hint), so the
recommendation is to ship it, not to hide PiP there. The alternative is to
hide the button on Wayland, which is less work and less useful.
