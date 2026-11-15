# TV mode

TV mode makes the application usable from a sofa, two or three metres from a
television, with a remote or a gamepad and no mouse. Turn it on in Settings >
Interface > TV mode, with the "TV mode" command of Ctrl+K, or for one run
with `--tv`. It changes at once, without a restart.

What changes:

- Larger text (24 px body text at 1920 by 1080), controls and targets of at
  least 64 px, larger cards, a 4 px focus ring, shorter animations.
- With "Keep clear of the TV's edges" (on by default) nothing that takes the
  focus is in the outer 5 % of the screen, for televisions that crop it.
- The arrows move the focus to the nearest control in that direction on
  every screen; Back always leads out (see [ui.md](ui.md) for how).
- Text fields open an on-screen keyboard.
- Sign-in leads with Quick Connect, which needs no typing.
- The player's display is a focus mode with a large timeline and named
  menu buttons.

## Keys

Every key below works the same from a keyboard, from a "keyboard" remote
(an air mouse or a media remote with a USB receiver, which sends keyboard
keys) and, through the input router, from a gamepad or the phone.

| Key | Everywhere | On-screen keyboard | Player |
|---|---|---|---|
| Arrows | Move the focus | Move over the keys; Up from the top row closes it | Display hidden: Left/Right seek 10 s (held: faster), Up/Down show it; shown: move the focus |
| Enter (Select) | Activate the focused control; on a text field, open the keyboard | Type the key | Display hidden: play/pause; on a button: press it |
| Esc, Backspace outside a field (Back) | Back to the previous screen (on sign-in: the previous step; on Watch together: an open room card first) | Close it, back on the field | Close the menu, then the display, then the player |
| Page Up / Page Down | About a page up or down | Erase / space | Live TV: next / previous channel |
| Menu key | The focused item's menu, else the sidebar and back | — | — |
| Ctrl+K | Commands and search | — | — |
| Media keys | Play/pause, next, previous | — | — |
| Channel Up / Down | — | — | Live TV: next / previous channel |

Seeking in the player in TV mode: a press moves 10 s at once. Held, the steps
grow (10 s, after about a second 30 s, then 60 s), the timeline shows where
playback will go, and one seek follows when the key is let go. In a
SyncPlay group that is one request to the group, not one per step.

The on-screen keyboard has English and Russian letters, symbols and digits,
Shift (one capital), the other language, the cursor left and right, space,
erase and Done. Done is Enter in the field: on the sign-in form it moves
from the user name to the password with the keyboard still open, and from
the password signs in. The field keeps the focus, so its text and cursor
also show above the keys (masked for a password). Address and e-mail
fields start in Latin letters. It is our own: Qt Virtual Keyboard is
GPL-3.0 only.

A physical keyboard is never in the way: as soon as it types a character
(or Backspace) the on-screen keyboard closes and stays away, and Enter in a
field is Enter again, until the gamepad or the phone is used. A remote that
sends only arrows and Enter does not count as typing.

## Gamepad

Buttons are read by position, not by label (SDL3's layout), so the bottom
button selects on every controller. Xbox and 8BitDo (in its X-input mode,
or in Switch mode with Nintendo labels) as an example:

| Position | Xbox | 8BitDo, Nintendo labels | Action |
|---|---|---|---|
| D-pad, left stick | D-pad, left stick | D-pad, left stick | Arrows (dead zone 0.35; held: repeat after 400 ms, then 8 per second) |
| Bottom face button | A | B | Select (Enter) |
| Right face button | B | A | Back |
| Top face button | Y | X | Search (Ctrl+K) |
| Left face button | X | Y | Not used |
| Left / right shoulder | LB / RB | L / R | Page Up / Page Down (on the keyboard: erase / space; Live TV: channel) |
| Left / right trigger | LT / RT | ZL / ZR | Seek back / forward 10 s |
| Start | Menu (≡) | Start (+) | Play/pause |
| Back | View (⧉) | Select (−) | Menu key |

Settings > Gamepad turns gamepads off, lets them work while the window is in
the background, and checks the buttons (each press shows its action instead
of acting; Back twice ends the check).

## From the phone

With Settings > Remote control > "Allow control from other devices" on, the Jellyfin app
on a phone (or Jellyfin Web, "Play on" this device) controls the
application through the server:

- Navigation: Up, Down, Left, Right, Select and Back, the same as the keys
  above (the on-screen keyboard included); Home, Search and Settings.
- Text: "Send text" (`SendString`) goes into the focused text field, as if
  typed, whether the on-screen keyboard is open or not. It is the easiest
  way to type a long password or search on a TV.
- Playback: play items here, pause, seek, next and previous, stop, volume
  and mute, audio and subtitle tracks, full screen, a message on the screen.
  In a SyncPlay group these act on the whole group.

From the same computer, `jellyfin-desktop --command play-pause` (and `next`,
`previous`, `stop`, `fullscreen`) does the same as the media keys; see
[integrations-api.md](integrations-api.md).

## HDMI-CEC: not supported

The TV's own remote over HDMI-CEC does not control the application, and the
application does not turn the TV on or switch its input.

Why: the graphics cards and integrated GPUs of PCs (NVIDIA, AMD, Intel) do
not connect the CEC line of their HDMI ports, so a PC cannot send or
receive CEC at all without extra hardware: a USB-CEC adapter between the PC
and the TV (Pulse-Eight) and its library, libcec. That is hardware almost no
user has, which CI cannot check and which we cannot test without it.

What to use instead: a gamepad, a keyboard remote (USB receiver), or the
Jellyfin app on the phone, all above.

When to reconsider: if users with a USB-CEC adapter ask for it, or a PC
platform starts to expose CEC through the operating system. The work then
is a libcec input source in `src/platform` that sends the same input
actions as the gamepad (the key mapping above), off by default; libcec is
GPL-2.0-or-later, compatible with this project's GPL-2.0 licence, and it
would be a pinned dependency with its own packaging for the installers.
