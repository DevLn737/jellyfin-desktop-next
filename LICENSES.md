# Third-party material

The program is GPL-2.0-or-later (see LICENSE). It includes:

- **Interface icons** (every `icons*` folder under `src/ui`): most are
  outlines from Google's Material Icons,
  [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0); the table
  below names the source of each file. An icon in an `icons-dark` folder is
  the same drawing as its twin in `icons`, with a dark fill for light themes.
  The `licenses_check` test fails when an icon file has no row here or a row
  names a file that is gone.
- **Manrope** typeface (`src/ui/theme/fonts`: Regular, Medium, SemiBold, Bold),
  [SIL Open Font License 1.1](src/ui/theme/fonts/LICENSE-Manrope.txt),
  copyright 2018 The Manrope Project Authors. Embedded unmodified; the licence
  text ships with the font files.
- **SDL3** 3.4.20 (gamepad input: the joystick, gamepad and HIDAPI
  subsystems only), [zlib licence](packaging/licenses/SDL3.txt) (copyright
  notice in that file). Built from the release archive pinned in
  `deps/versions.json` (`cmake/JfdSdl3.cmake`) and linked into the
  executable, unmodified. It contains **HIDAPI**, which offers GPL-3.0, a
  BSD-style or its original licence; this program uses it under the
  [BSD-style licence](packaging/licenses/HIDAPI.txt). Both texts ship in the
  packages (AppImage: `usr/share/licenses/jellyfin-desktop-next`, MSI: the
  `licenses` folder).
- **doctest** (`third_party/doctest`, tests only), MIT; **nlohmann/json** and
  **Monocypher** under their own licences in `third_party/`.

## Icons, one row per file

"Material Icons `name`" is the icon of that name in
[google/material-design-icons](https://github.com/google/material-design-icons)
(the filled "Material Icons" style, 24 px), with its path data unchanged or
only reformatted.

| File | Source | Licence |
|---|---|---|
| `src/ui/controls/icons/back.svg` | Material Icons `arrow_back` | Apache-2.0 |
| `src/ui/controls/icons/check.svg` | Material Icons `check` | Apache-2.0 |
| `src/ui/controls/icons/close.svg` | Material Icons `close` | Apache-2.0 |
| `src/ui/controls/icons/down.svg` | Material Icons `keyboard_arrow_down` | Apache-2.0 |
| `src/ui/controls/icons/error.svg` | Material Icons `error` | Apache-2.0 |
| `src/ui/controls/icons/group.svg` | Material Icons `people` | Apache-2.0 |
| `src/ui/controls/icons/home.svg` | Material Icons `home` | Apache-2.0 |
| `src/ui/controls/icons/library.svg` | Material Icons `video_library` | Apache-2.0 |
| `src/ui/controls/icons/menu.svg` | Material Icons `menu` | Apache-2.0 |
| `src/ui/controls/icons/play.svg` | Material Icons `play_arrow` | Apache-2.0 |
| `src/ui/controls/icons/search.svg` | Material Icons `search` | Apache-2.0 |
| `src/ui/controls/icons/settings.svg` | Material Icons `settings` | Apache-2.0 |
| `src/ui/controls/icons/up.svg` | Material Icons `keyboard_arrow_up` | Apache-2.0 |
| `src/ui/controls/icons-dark/back.svg` | Material Icons `arrow_back` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/check.svg` | Material Icons `check` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/close.svg` | Material Icons `close` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/down.svg` | Material Icons `keyboard_arrow_down` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/error.svg` | Material Icons `error` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/group.svg` | Material Icons `people` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/home.svg` | Material Icons `home` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/library.svg` | Material Icons `video_library` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/menu.svg` | Material Icons `menu` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/play.svg` | Material Icons `play_arrow` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/search.svg` | Material Icons `search` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/settings.svg` | Material Icons `settings` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/up.svg` | Material Icons `keyboard_arrow_up` (dark fill) | Apache-2.0 |
| `src/ui/music/icons/expand.svg` | Material Icons `keyboard_arrow_down` | Apache-2.0 |
| `src/ui/music/icons/more.svg` | Material Icons `more_vert` | Apache-2.0 |
| `src/ui/music/icons/music.svg` | Material Icons `music_note` | Apache-2.0 |
| `src/ui/music/icons/next.svg` | Material Icons `skip_next` | Apache-2.0 |
| `src/ui/music/icons/pause.svg` | Material Icons `pause` | Apache-2.0 |
| `src/ui/music/icons/previous.svg` | Material Icons `skip_previous` | Apache-2.0 |
| `src/ui/music/icons/queue.svg` | Material Icons `queue_music` | Apache-2.0 |
| `src/ui/music/icons/repeat.svg` | Material Icons `repeat` | Apache-2.0 |
| `src/ui/music/icons/repeatOne.svg` | Material Icons `repeat_one` | Apache-2.0 |
| `src/ui/music/icons/shuffle.svg` | Material Icons `shuffle` | Apache-2.0 |
| `src/ui/player/icons/audio.svg` | Material Icons `music_note` | Apache-2.0 |
| `src/ui/player/icons/episodes.svg` | Material Icons `playlist_play` | Apache-2.0 |
| `src/ui/player/icons/forward.svg` | Material Icons `replay`, mirrored | Apache-2.0 |
| `src/ui/player/icons/fullscreen.svg` | Material Icons `fullscreen` | Apache-2.0 |
| `src/ui/player/icons/fullscreenExit.svg` | Material Icons `fullscreen_exit` | Apache-2.0 |
| `src/ui/player/icons/next.svg` | Material Icons `skip_next` | Apache-2.0 |
| `src/ui/player/icons/pause.svg` | Material Icons `pause` | Apache-2.0 |
| `src/ui/player/icons/pip.svg` | Material Icons `picture_in_picture` | Apache-2.0 |
| `src/ui/player/icons/previous.svg` | Material Icons `skip_previous` | Apache-2.0 |
| `src/ui/player/icons/replay.svg` | Material Icons `replay` | Apache-2.0 |
| `src/ui/player/icons/segments.svg` | Part of this program: a plain geometric shape, no Material Icons match found | GPL-2.0-or-later |
| `src/ui/player/icons/subtitles.svg` | Material Icons `subtitles` | Apache-2.0 |
| `src/ui/player/icons/tune.svg` | Material Icons `tune` | Apache-2.0 |
| `src/ui/player/icons/volume.svg` | Material Icons `volume_up` | Apache-2.0 |
| `src/ui/player/icons/volumeOff.svg` | Material Icons `volume_off` | Apache-2.0 |
| `src/ui/requests/icons/partial.svg` | Part of this program: a plain geometric shape, no Material Icons match found | GPL-2.0-or-later |
| `src/ui/requests/icons/pending.svg` | Material Icons `schedule` | Apache-2.0 |
| `src/ui/requests/icons/processing.svg` | Material Icons `sync` | Apache-2.0 |
| `src/ui/requests/icons/requests.svg` | Material Icons `add_circle_outline` | Apache-2.0 |
| `src/ui/requests/icons-dark/partial.svg` | Part of this program: a plain geometric shape, no Material Icons match found (dark fill) | GPL-2.0-or-later |
| `src/ui/requests/icons-dark/pending.svg` | Material Icons `schedule` (dark fill) | Apache-2.0 |
| `src/ui/requests/icons-dark/processing.svg` | Material Icons `sync` (dark fill) | Apache-2.0 |
| `src/ui/requests/icons-dark/requests.svg` | Material Icons `add_circle_outline` (dark fill) | Apache-2.0 |
