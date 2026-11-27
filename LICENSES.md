# Third-party material

The program is GPL-2.0-or-later (see LICENSE). It includes:

- **Interface icons** (every `icons*` folder under `src/ui`): most are
  outlines from Google's Material Icons,
  [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0); the table
  below names the source of each file. An icon in an `icons-dark` folder is
  the same drawing as its twin in `icons`, with a dark fill for light themes.
  The site icons in `src/ui/library/icons/sites` (links to a film on IMDb,
  TMDB, AniList, MyAnimeList, Shikimori) take their marks from
  [Simple Icons](https://github.com/simple-icons/simple-icons), CC0-1.0;
  the TVDB icon is from
  [Dashboard Icons](https://github.com/homarr-labs/dashboard-icons),
  Apache-2.0;
  the marks are trademarks of their owners, used only to name the site a
  link opens.
  The `licenses_check` test fails when an icon file has no row here or a row
  names a file that is gone.
- **Nunito** typeface (`src/ui/theme/fonts`: Regular, Medium, SemiBold, Bold,
  ExtraBold, Black), [SIL Open Font License 1.1](src/ui/theme/fonts/LICENSE-Nunito.txt),
  copyright 2014 The Nunito Project Authors. Embedded unmodified; the licence
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
- **Bundled with the packages** (not in this repository): Qt, libmpv,
  FFmpeg, libplacebo, libass, ICU, the Vulkan loader, Mesa llvmpipe and
  Microsoft's runtime redistributables, plus the build system's libraries in
  the AppImage. [packaging/licenses/THIRD-PARTY-NOTICES.txt](packaging/licenses/THIRD-PARTY-NOTICES.txt)
  lists each with its version, licence and source, and the licence texts are
  next to it (sources of the texts: `SOURCES.txt` there). The Windows
  `libmpv-2.dll` links about ninety libraries statically: `libmpv-windows.txt`
  there has the licence of each, and the release `libmpv-windows-<build>` of
  this repository their source code
  ([deps/libmpv-windows-sources.json](deps/libmpv-windows-sources.json),
  `.github/workflows/libmpv-sources.yml`). Both packages ship
  the folder; the MSI build fails on a DLL and the AppImage build on a
  library that has no notice, and `licenses_check` fails when a text or a
  pinned version is missing from the notices.

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
| `src/ui/controls/icons/delete.svg` | Material Icons `delete` | Apache-2.0 |
| `src/ui/controls/icons/down.svg` | Material Icons `keyboard_arrow_down` | Apache-2.0 |
| `src/ui/controls/icons/left.svg` | Material Icons `keyboard_arrow_left` | Apache-2.0 |
| `src/ui/controls/icons/right.svg` | Material Icons `keyboard_arrow_right` | Apache-2.0 |
| `src/ui/controls/icons/tune.svg` | Material Icons `tune` | Apache-2.0 |
| `src/ui/controls/icons/sort.svg` | Material Icons `sort` | Apache-2.0 |
| `src/ui/controls/icons/filter.svg` | Material Icons `filter_alt` | Apache-2.0 |
| `src/ui/controls/icons/download.svg` | Material Icons `download` | Apache-2.0 |
| `src/ui/controls/icons/downloaded.svg` | Material Icons `download_done` | Apache-2.0 |
| `src/ui/controls/icons/error.svg` | Material Icons `error` | Apache-2.0 |
| `src/ui/controls/icons/gamepad.svg` | Material Icons `sports_esports` | Apache-2.0 |
| `src/ui/controls/icons/group.svg` | Material Icons `people` | Apache-2.0 |
| `src/ui/controls/icons/home.svg` | Material Icons `home` | Apache-2.0 |
| `src/ui/controls/icons/library.svg` | Material Icons `video_library` | Apache-2.0 |
| `src/ui/controls/icons/menu.svg` | Material Icons `menu` | Apache-2.0 |
| `src/ui/controls/icons/pause.svg` | Material Icons `pause` | Apache-2.0 |
| `src/ui/controls/icons/play.svg` | Material Icons `play_arrow` | Apache-2.0 |
| `src/ui/controls/icons/retry.svg` | Material Icons `refresh` | Apache-2.0 |
| `src/ui/controls/icons/favorite.svg` | Material Icons `favorite` | Apache-2.0 |
| `src/ui/controls/icons/favorite-border.svg` | Material Icons `favorite_border` | Apache-2.0 |
| `src/ui/controls/icons/info.svg` | Material Icons `info_outline` | Apache-2.0 |
| `src/ui/controls/icons/more.svg` | Material Icons `more_horiz` | Apache-2.0 |
| `src/ui/controls/icons/search.svg` | Material Icons `search` | Apache-2.0 |
| `src/ui/controls/icons/settings.svg` | Material Icons `settings` | Apache-2.0 |
| `src/ui/controls/icons/tv.svg` | Material Icons `tv` | Apache-2.0 |
| `src/ui/controls/icons/logout.svg` | Material Icons `exit_to_app` | Apache-2.0 |
| `src/ui/controls/icons/up.svg` | Material Icons `keyboard_arrow_up` | Apache-2.0 |
| `src/ui/controls/icons/subtitles.svg` | Material Icons `subtitles` | Apache-2.0 |
| `src/ui/controls/icons/audio.svg` | Material Icons `volume_up` | Apache-2.0 |
| `src/ui/controls/icons/window.svg` | Material Icons `desktop_windows` | Apache-2.0 |
| `src/ui/controls/icons/music.svg` | Material Icons `music_note` | Apache-2.0 |
| `src/ui/controls/icons/link.svg` | Material Icons `link` | Apache-2.0 |
| `src/ui/controls/icons/palette.svg` | Material Icons `palette` | Apache-2.0 |
| `src/ui/controls/icons-dark/subtitles.svg` | Material Icons `subtitles` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/audio.svg` | Material Icons `volume_up` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/window.svg` | Material Icons `desktop_windows` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/music.svg` | Material Icons `music_note` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/link.svg` | Material Icons `link` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/palette.svg` | Material Icons `palette` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/back.svg` | Material Icons `arrow_back` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/check.svg` | Material Icons `check` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/close.svg` | Material Icons `close` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/delete.svg` | Material Icons `delete` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/down.svg` | Material Icons `keyboard_arrow_down` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/left.svg` | Material Icons `keyboard_arrow_left` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/right.svg` | Material Icons `keyboard_arrow_right` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/tune.svg` | Material Icons `tune` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/sort.svg` | Material Icons `sort` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/filter.svg` | Material Icons `filter_alt` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/download.svg` | Material Icons `download` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/downloaded.svg` | Material Icons `download_done` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/error.svg` | Material Icons `error` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/gamepad.svg` | Material Icons `sports_esports` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/group.svg` | Material Icons `people` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/home.svg` | Material Icons `home` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/library.svg` | Material Icons `video_library` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/menu.svg` | Material Icons `menu` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/pause.svg` | Material Icons `pause` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/play.svg` | Material Icons `play_arrow` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/retry.svg` | Material Icons `refresh` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/favorite.svg` | Material Icons `favorite` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/favorite-border.svg` | Material Icons `favorite_border` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/info.svg` | Material Icons `info_outline` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/more.svg` | Material Icons `more_horiz` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/search.svg` | Material Icons `search` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/settings.svg` | Material Icons `settings` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/tv.svg` | Material Icons `tv` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/logout.svg` | Material Icons `exit_to_app` (dark fill) | Apache-2.0 |
| `src/ui/controls/icons-dark/up.svg` | Material Icons `keyboard_arrow_up` (dark fill) | Apache-2.0 |
| `src/ui/library/icons/livetv.svg` | Material Icons `live_tv` | Apache-2.0 |
| `src/ui/library/icons/sites/anilist.svg` | Simple Icons `anilist` (16.38.1) on a rounded tile in the site's colours | CC0-1.0 (the mark is a trademark of its owner) |
| `src/ui/library/icons/sites/imdb.svg` | Simple Icons `imdb` (16.38.1) on a rounded tile in the site's colours | CC0-1.0 (the mark is a trademark of its owner) |
| `src/ui/library/icons/sites/myanimelist.svg` | Simple Icons `myanimelist` (16.38.1) on a rounded tile in the site's colours | CC0-1.0 (the mark is a trademark of its owner) |
| `src/ui/library/icons/sites/shikimori.svg` | Simple Icons `shikimori` (16.38.1) on a rounded tile in the site's colours | CC0-1.0 (the mark is a trademark of its owner) |
| `src/ui/library/icons/sites/tmdb.svg` | Simple Icons `themoviedatabase` (16.38.1) on a rounded tile in the site's colours | CC0-1.0 (the mark is a trademark of its owner) |
| `src/ui/library/icons/sites/tvdb.svg` | [Dashboard Icons](https://github.com/homarr-labs/dashboard-icons) `svg/tvdb.svg`, unchanged | Apache-2.0 (the mark is a trademark of its owner) |
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
| `src/ui/player/icons-dark/forward.svg` | Material Icons `replay`, mirrored (dark fill) | Apache-2.0 |
| `src/ui/player/icons-dark/pause.svg` | Material Icons `pause` (dark fill) | Apache-2.0 |
| `src/ui/player/icons/pip.svg` | Material Icons `picture_in_picture` | Apache-2.0 |
| `src/ui/player/icons/previous.svg` | Material Icons `skip_previous` | Apache-2.0 |
| `src/ui/player/icons/replay.svg` | Material Icons `replay` | Apache-2.0 |
| `src/ui/player/icons/segments.svg` | Part of this program: a plain geometric shape, no Material Icons match found | GPL-2.0-or-later |
| `src/ui/player/icons/speed.svg` | Material Icons `speed` | Apache-2.0 |
| `src/ui/player/icons/speedGauge.svg` | Part of this program: a plain arc (the speedometer's rim, the speed is written inside) | GPL-2.0-or-later |
| `src/ui/player/icons/subtitles.svg` | Material Icons `subtitles` | Apache-2.0 |
| `src/ui/player/icons/tune.svg` | Material Icons `tune` | Apache-2.0 |
| `src/ui/player/icons/volume.svg` | Material Icons `volume_up` | Apache-2.0 |
| `src/ui/player/icons/volumeOff.svg` | Material Icons `volume_off` | Apache-2.0 |
