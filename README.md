<p align="center">
  <img src="packaging/icons/jellyfin-desktop-next.svg" width="112" alt="">
</p>

<h1 align="center">Jellyfin Desktop Next</h1>

<p align="center">
  A native desktop client for <a href="https://jellyfin.org">Jellyfin</a> on Windows and Linux,<br>
  built around watching together.
</p>

<p align="center">
  <a href="https://github.com/DevLn737/jellyfin-desktop-next/releases"><img alt="Latest pre-release" src="https://img.shields.io/github/v/release/DevLn737/jellyfin-desktop-next?include_prereleases&label=download&color=3f93ad"></a>
  <a href="https://github.com/DevLn737/jellyfin-desktop-next/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/DevLn737/jellyfin-desktop-next/actions/workflows/ci.yml/badge.svg?branch=main"></a>
  <img alt="Windows | Linux" src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-555">
  <a href="LICENSE"><img alt="GPL-2.0-or-later" src="https://img.shields.io/badge/license-GPL--2.0--or--later-555"></a>
</p>

<p align="center">
  <a href="#download">Download</a> ·
  <a href="#features">Features</a> ·
  <a href="#screenshots">Screenshots</a> ·
  <a href="#building">Building</a> ·
  <a href="#по-русски">По-русски</a>
</p>

<p align="center">
  <img src="docs/screenshots/home.jpg" alt="Home screen: a banner with what you are watching and rows of films and series">
</p>

> **Beta.** The client is in active development and ships as pre-releases.
> Things change from one beta to the next; please report what breaks in
> [Issues](https://github.com/DevLn737/jellyfin-desktop-next/issues).

## Download

Get the newest build from the **[Releases page](https://github.com/DevLn737/jellyfin-desktop-next/releases)**
(the latest pre-release is at the top).

| System | File | How to install |
| --- | --- | --- |
| Windows 10 (1903) or newer, 64-bit | `jellyfin-desktop-next-<version>-x64.msi` | Run it. It installs for your user, without administrator rights. The installer is not code-signed, so SmartScreen warns on first run: **More info → Run anyway**. |
| Linux (glibc 2.39+: Ubuntu 24.04, Fedora 40, Debian 13, Arch and newer) | `jellyfin-desktop-next-x86_64.AppImage` | `chmod +x jellyfin-desktop-next-x86_64.AppImage`, then run it. |

After that the app updates itself: it checks for new releases, verifies the
download's signature and offers **Restart and update**.

The client is tested against Jellyfin server 12.2. Changing the speed for the
whole room needs the server fork listed under [Related repositories](#related-repositories);
everything else, watching together included, works with a stock server.

## Features

- **Native and light.** C++20, Qt 6 Quick and libmpv. No embedded browser,
  no Chromium, no Jellyfin Web inside.
- **Playback by mpv.** Hardware decoding, ASS/SSA and PGS subtitles, external
  audio and subtitle files, chapters, Skip intro, audio and subtitle delays,
  speed, picture in picture, stats for nerds, and switching the display's
  refresh rate to the film's frame rate. Direct Play first, transcoding when
  the server has to.
- **Watch together.** Rooms on your server (SyncPlay) with friends on this
  client or in Jellyfin Web: the same position, pause and speed for everyone,
  while each person picks their own audio track and subtitles. Drift is
  corrected by a gentle speed nudge, not by jumps.
- **Your library, redesigned.** Home with a banner and rows, films and series
  with seasons and episodes, filters and sorting, collections, people,
  search across everything (Ctrl+K), favourites and watched marks.
- **Music.** Albums, artists, playlists, genres, lyrics, a queue and a
  mini-player.
- **Live TV.** Channels and the guide; Page Up and Down change the channel.
- **Downloads.** Films and episodes for offline viewing, with their external
  audio and subtitles, in a readable Series/Season folder layout.
- **On the sofa.** TV mode for a remote or a gamepad (Xbox, PlayStation,
  8BitDo): large text, arrow navigation everywhere, an on-screen keyboard and
  Quick Connect sign-in.
- **Remote control.** "Play on" from Jellyfin Web and the phone apps.
- **Integrations.** Discord Rich Presence (with privacy options per kind and
  library), media keys and the system's now-playing panel, and requests
  through Seerr.
- **Russian and English** interface.

## Screenshots

| | |
| --- | --- |
| ![A series page: title, details, seasons and episodes](docs/screenshots/series.jpg) | ![A film page](docs/screenshots/movie.jpg) |
| **Series** with seasons, episodes and what is left to watch | **Film** page |
| ![The player with the controls shown](docs/screenshots/player.jpg) | ![Watch together: rooms on the server](docs/screenshots/watch-together.jpg) |
| **Player** with chapters and the episode line | **Watch together**: rooms on your server |
| ![A library with the filters panel open](docs/screenshots/library.jpg) | ![Music overview with albums](docs/screenshots/music.jpg) |
| **Library** with filters and sorting | **Music** |
| ![Settings with sections on the left](docs/screenshots/settings.jpg) | |
| **Settings** with search | |

The screenshots are taken by the UI tests on a demo catalogue whose pictures
are drawn by the tests themselves; [scripts/readme-screenshots.sh](scripts/readme-screenshots.sh)
takes them again.

## Architecture

The decisions and their reasons are recorded in [docs/adr](docs/adr):

1. [Stack: C++20, Qt 6 Quick, libmpv](docs/adr/0001-stack.md)
2. [Module boundaries](docs/adr/0002-module-boundaries.md)
3. [Playback session and SyncPlay model](docs/adr/0003-playback-session-model.md)

The plan and its exit criteria are in [docs/roadmap.md](docs/roadmap.md).

## Related repositories

| Repository | Role |
| --- | --- |
| [DevLn737/jellyfin](https://github.com/DevLn737/jellyfin/tree/syncplay-rate/12.2) | Server fork with the SyncPlay playback-rate extension |
| [DevLn737/jellyfin-web](https://github.com/DevLn737/jellyfin-web/tree/syncplay-rate/12.2) | Web fork with the same extension, for browser participants |
| [DevLn737/jellyfin-syncplay-speed](https://github.com/DevLn737/jellyfin-syncplay-speed) | Server image build, release manifest and protocol documentation |
| [DevLn737/jellyfin-desktop](https://github.com/DevLn737/jellyfin-desktop) | The previous Windows client, based on Jellyfin Media Player (fixes only) |

## Testing SyncPlay

[tools/syncplay-harness](tools/syncplay-harness) runs the forked Jellyfin 12.2
server in Docker and checks SyncPlay drift, pause, seek, rate changes and
rejoin with Jellyfin Web clients in headless Chromium. CI runs it on every
change to the harness.

## Building

The Qt-free core always builds. The Qt modules (`platform`, `jellyfin-api`,
`library`) build when Qt 6.4+ with Qt WebSockets is found; see
[docs/jellyfin-api.md](docs/jellyfin-api.md).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Requires CMake 3.24+ and a C++20 compiler (MSVC 2022, GCC 12+, Clang 15+ / Apple Clang 15+).

The application also needs Qt 6 (Quick, OpenGL) and libmpv; add `-DJFD_BUILD_APP=ON`. On Windows it renders with Direct3D 11 when libmpv has the D3D11 render API (client API 2.6, as pinned), otherwise with OpenGL.
### Modules and translations

Each module is a directory under `src/` with its own `CMakeLists.txt`; the root
file lists every planned module and adds the ones that exist
([cmake/JfdModules.cmake](cmake/JfdModules.cmake)).

Interface strings are English in the source and translated per module in
`src/<module>/i18n/<module>_ru.ts` ([cmake/JfdTranslations.cmake](cmake/JfdTranslations.cmake)).
After changing strings, run `cmake --build build --target update_translations`,
translate the new entries and commit the `.ts` files. The `translations_check`
test fails on any string that is missing, untranslated or no longer used.
Modules with plural strings (`qsTr("%n item(s)", "", n)`) also keep a
`<module>_en.ts` with only those, so English reads "5 items"
(`jfd_add_translations(<target> PLURALS)`).

CI uses the versions pinned in [deps/versions.json](deps/versions.json) and passes
`-DJFD_ENFORCE_PINS=ON` to reject any other.

- **Linux:** libmpv from the distribution (`libmpv-dev`), found through pkg-config.
- **Windows:** from a Visual Studio developer PowerShell, `scripts/fetch-windows-deps.ps1`
  downloads and verifies the pinned Qt, libmpv and Vulkan loader; pass the Qt prefix
  as `CMAKE_PREFIX_PATH` and the `MPV_INCLUDE_DIR` and `MPV_LIBRARY` it prints to CMake.
  The Qt `bin` directory and `build/deps/runtime` must be on `PATH` when running.

Any platform can point CMake at a specific libmpv with `-DMPV_INCLUDE_DIR=... -DMPV_LIBRARY=...`.

## Packages

Releases are built and signed by `.github/workflows/package.yml`; CI also
builds both packages on every pull request and keeps them as workflow
artifacts for 30 days. See [packaging/README.md](packaging/README.md) for how
they are built.

- **Windows:** `jellyfin-desktop-next-<version>-x64.msi`. Installs for the
  current user without administrator rights into
  `%LOCALAPPDATA%\Programs\Jellyfin Desktop Next`, with Start menu and desktop
  shortcuts. Windows 10 1903 or newer, 64-bit. The installer is **not signed**
  (there is no code-signing certificate), so SmartScreen warns on first run:
  choose "More info", then "Run anyway". Uninstalling removes the program and
  shortcuts and keeps settings; `msiexec /x <msi> REMOVEPROFILE=1` removes
  settings and cache too.
- **Linux:** `jellyfin-desktop-next-x86_64.AppImage`, one file for all
  distributions with glibc 2.39 or newer: Ubuntu 24.04, Fedora 40, Debian 13,
  Arch and newer. It carries Qt and the same libmpv build (mpv commit, FFmpeg)
  as Windows; graphics drivers, VA-API, PipeWire/PulseAudio and GnuTLS come
  from the system. Make it executable and run it. Flatpak comes later.

**Updates.** Both builds check for a newer release (Settings, Integrations)
and update themselves. The client downloads the release file and its
`.minisig`, checks the minisign signature against the key built into the
client, and offers "Restart and update" in the toast once nothing plays and
you are not in a SyncPlay group; if the download or the check fails, the toast
links to the release page instead.

- **Linux:** the AppImage is replaced in place (the folder must be writable).
  The old file is kept until the new version confirms its start; if it exits
  or hangs within 60 seconds, the old file is put back and started, and it
  reports that the new version did not start.
- **Windows:** `jellyfin-desktop-next-<version>-x64.msi` is installed over the
  running copy with `msiexec` (a major upgrade of the same product; per user,
  no administrator rights) by a PowerShell script that waits for the app to
  exit and starts it again. If the installer fails, Windows Installer restores
  the old version, which is started and reports that the new version did not
  install (the installer log is `update-install.log` in the app's
  `update` data folder). Only a copy installed by the MSI updates itself.

## По-русски

**Jellyfin Desktop Next** — нативный клиент Jellyfin для Windows и Linux,
сделанный вокруг совместного просмотра. Сейчас это бета: новые версии выходят
на [странице релизов](https://github.com/DevLn737/jellyfin-desktop-next/releases).

**Как установить.** Windows: скачайте `.msi` и запустите (права администратора
не нужны; при первом запуске SmartScreen предупредит, нажмите «Подробнее» →
«Выполнить в любом случае»). Linux: скачайте `.AppImage`, сделайте его
исполняемым (`chmod +x`) и запустите. Дальше приложение обновляется само.

**Что умеет.**

- Воспроизведение через mpv: аппаратное декодирование, ASS и PGS субтитры,
  внешние дорожки, главы, пропуск заставки, картинка в картинке.
- «Смотреть вместе»: комнаты на вашем сервере, общая позиция, пауза и
  скорость, а озвучку и субтитры каждый выбирает себе сам.
- Главная, библиотека с фильтрами, страницы фильмов и сериалов, поиск
  (Ctrl+K), музыка, ТВ-каналы, загрузки для просмотра без сети.
- Режим ТВ для пульта и геймпада, Discord, управление с телефона, Seerr.
- Интерфейс на русском и английском.

## License

GPL-2.0-or-later. Parts of this client are derived from
[Jellyfin Media Player](https://github.com/jellyfin/jellyfin-desktop).
