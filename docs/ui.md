# Interface modules

Every screen is built from the same QML modules, so screens written in
parallel look and behave alike. Each module is a static library linked into
`jellyfin-desktop` (`src/desktop`), which imports its plugin with
`Q_IMPORT_QML_PLUGIN`.

| Module | Directory | Owner | Contents |
| --- | --- | --- | --- |
| `Jfd.Ui.Theme` | `src/ui/theme` | shell branch | `Theme` singleton |
| `Jfd.Ui.Controls` | `src/ui/controls` | shell branch | building blocks below |
| `Jfd.Ui.Shell` | `src/ui/shell` | shell branch | `Navigator`, `AppShell`, `RoutePage` |
| `Jfd.Ui.Settings` | `src/ui/settings` | shell branch | `AppSettings`, settings screen |
| `Jfd.Ui.Library` | `src/ui/library` | library branch | home, library, item, search pages |
| `Jfd.Ui.Player` | `src/ui/player` | player branch | player page, OSD, track menus, episodes panel |
| `Jfd.Ui.Group` | `src/ui/group` | SyncPlay branch | group page and OSD |

## Theme

`Theme.background`, `surface`, `surfaceRaised`, `surfaceHover`, `border`,
`accent`, `accentHover`, `accentText`, `text`, `textSecondary`,
`textDisabled`, `error`, `success`, `focusRing`, `scrim`, `progress`;
`fontSizeSmall` / `Body` / `Large` / `Title` / `Headline`,
`fontWeightNormal` / `Bold`; `spacingTiny` (4) / `spacingSmall` (8) /
`spacing` (12) / `spacingLarge` (20) / `spacingHuge` (32), `radius`,
`controlHeight`, `iconSize`, `posterWidth`, `thumbWidth`, `squareWidth`,
`posterAspect`, `thumbAspect`, `squareAspect`; `cardWidthFor(shape)` and
`cardAspectFor(shape)` for the card shapes `poster` (2:3), `thumb` (16:9),
`square` and `circle` (the same size as square, cut round, for artists);
`durationFast` / `Normal` / `Slow` and
`easing`. Durations are halved when `Theme.reducedEffects` is on (the
"Simplified effects" setting, automatically on with integrated or software
graphics), so always animate with them; blur only when `Theme.blurEnabled`. The interface scale
setting applies through `QT_SCALE_FACTOR`; do not scale sizes by hand.

The colours come from a palette: `Theme.colors` is a map from the names
above to colours (`Palettes.dark` is the built-in one). Screens use only the
names, never literal colours. **Names in `Theme` are only added, never
renamed or removed**, while other branches build screens on them.

What is drawn over the video (the player's controls and menus, the group's
on-screen display) uses `Overlay` instead of `Theme`: the same colour names,
fixed dark, the same in every theme. The video stays dark whatever the page
theme is.

### The "Depth" look (default)

`Palettes.depth` / `depthPalette()`: a blue-black page (`#0b0e15`), panels
of translucent glass, hairline white edges (8 %), a muted teal accent
(`#5fb3c6`) used only for progress, the active entry and focus, near-white
text, and a pale main button with dark text. Nunito (SIL OFL 1.1, in
`src/ui/theme/fonts` with its licence) is the typeface; the application
loads it, and a system font is used if that fails. Radius 12.

A palette may add optional names (`optionalColorNames()`): `glassFill`,
`glassBorder`, `primary`, `primaryText`. `Theme` has a fallback for each
(`glassFill` is the surface at 85 %, `primary` is the accent), so older
palettes and theme files work unchanged. New `Theme` tokens: `panelFill`
and `popupFill` (glass with effects, nearly solid with simplified effects),
`radiusLarge`, `shadowColor` / `shadowRadius` / `shadowOffset` (one light
shadow for popups), `minHitSize` (44, for TV mode) and `focusRingOffset`,
`durationLong`. `Theme.sidebarWidth` is 216.

Rules: glass only when `Theme.blurEnabled` and not `Theme.reducedEffects`;
no blur is drawn over the live video (the player's controls read
`Theme.colorsFor(item)`, so they use `Overlay`, which stays the dark
palette, and a test checks this); at most one blur per screen, from a
static snapshot. The panels draw no blur yet (Qt 6.4 is the minimum), only
the translucent fill. Settings: Theme "Depth" (default), Dark, Light, Like
the system (Depth when the system is dark); accent "Theme default" (the
look's own accent) or a preset or custom colour.

### Themes

Settings > Appearance: the theme (dark, light, like the system, or a theme
file), the accent colour (eight presets or a custom `#rrggbb`) and
"Simplified effects". Everything changes at once, with no restart. A theme
is a palette (`Theme.colors`) made in C++ by `ThemePalette.h`
(`darkPalette()`, `lightPalette()`, `withAccent()`), which also derives
`accentHover`, `accentText` (black or white, whichever reads better),
`focusRing` and `progress` from the accent and checks WCAG AA contrast.
`Palettes.qml` must equal the C++ palettes (a test checks).

Theme files are JSON in the `themes` folder next to the settings file
(at most 64 KiB; `ThemeFile.h` has the format): `format` 1, `name`, `base`
("dark" or "light"), `colors` by the names above, optionally `radius` and
`fontFamily` (an installed font's name, never a path). Unknown keys are
ignored, a newer `format` is refused with a message, low contrast is
accepted with a warning. Drop a `.json` on the window to add one; "Save the
current theme as a file" writes a starting point. If a theme makes the
interface unreadable: Ctrl+K "Reset theme", or start with `--reset-theme`.

Controls take their colours from `Theme.colorsFor(item)`, which uses the
`colorScope` of the nearest ancestor that has one (the player sets
`Overlay.colors`), so a page can be light while the video's controls stay
dark. `Icon` shows the dark twin from `icons-dark/` when its `color` (the
text colour by default) is dark: add both files for a new icon.

### Window sizes

Sizes are logical pixels (the system scale and the interface scale are
already applied). `Theme.sizeClass` is `compact` under 900, `normal`, or
`spacious` over 1600, from the window width (the shell sets
`Theme.windowWidth`). In a compact window the sidebar is an icon rail and
the menu button opens it over the page; `posterWidth` (110 to 220 px),
`thumbWidth` and `pageMargin` follow the class, so use them instead of
fixed numbers. The smallest window is 640 by 420.

### Ctrl+K

`CommandPalette` (in `Jfd.Ui.Library`, placed over the shell in `Main.qml`)
opens with Ctrl+K, or Ctrl+/ when that is taken, on any page, the video
player included. The K key is recognised by its native code, so it works on
a Russian layout on Windows and Wayland. The key is fixed, not a setting.
The first block, "Commands", lists what `CommandRegistry` offers at that
moment (a module adds its own: `commands.add({id, title, available, run})`
with a title already translated; the shell registers Go back and one
"Open <name>" for each sidebar destination); the second, "Media", is a
search of its own (`Library.createSearch`, 200 ms after typing stops, old
answers dropped). Up, Down, Enter and Esc work; the last command used comes
first next time. While it is open the player's keys do not reach the player.

## Controls

| Component | Main properties and signals |
| --- | --- |
| `ActionButton` | `text`, `iconSource`, `primary`, `clicked()` |
| `IconButton` | `iconSource`, `toolTipText`, `clicked()` |
| `Icon`, `Icons` | `Icon { source: Icons.play; size: 22 }`; icons: home, search, group, settings, back, close, play, check, error, library, menu, up, down, gamepad |
| `InputField` | `label`, `text`, `placeholderText`, `errorText`, `password`, `accepted()`; `keyboardOnSelect` (default on: Select opens the on-screen keyboard in TV mode) |
| `ToggleSwitch` | `checked`, `toggled()` (Space or Enter) |
| `ChoiceBox` | `model: [{ value, label }]`, `currentValue`, `indexOfValue()`, `activated()`; Enter opens the list; in TV mode Up and Down move the focus, not the value (`navigationKeys`) |
| `ValueSlider` | `from`, `to`, `stepSize`, `value`, `moved()`, `accepted()` (Enter), `trackHeight`, `handleSize` |
| `PosterCard` | `title`, `subtitle`, `imageSource`, `shape` ("poster", "thumb", "square"), `progress` (0..1), `played`, `unplayedCount`, `cardWidth`, `clicked()` |
| `RoomCard` | a room of watching together: `text` (its name), `roomState` ("idle", "waiting", "paused", "playing"), `participants` (names), `selected` (its card is open), `clicked()` (Enter, Select) |
| `CardRow` | `title`, `model`, `delegate`, `cardHeight`, `currentIndex`, `view` |
| `CardGrid` | `model`, `delegate`, `cardWidth`, `cardHeight`, `currentIndex`, `view` |
| `ListItem` | `text`, `subtitle`, `trailingText`, `iconSource`, `highlighted`, `clicked()` |
| `SectionHeader`, `Heading` | text styles for a group title and a page title |
| `AppDialog` | `title`, `text`, `acceptText`, `rejectText`, `open()`, `accepted()`, `rejected()` |
| `Toast` | `show(text, kind, linkText, link)` with kind "info" or "error"; a card in the bottom right corner, the link as a button; use the shell's |
| `LoadingIndicator` | `running` |
| `FocusFrame` | the keyboard focus ring; `shown: control.visualFocus` |
| `GlassPanel` | a translucent panel with a hairline edge (`popup: true` for menus); nearly solid with simplified effects |
| `PopupMenu` | `anchorItem` (the button), `entries`, `currentIndex`, `disabledIndices`, `alignRight`, `open()`, `activated(index)`; 8 px under the button, above it if there is no room, always inside the window; the menus of the player and music move to it |
| `SkeletonBox` | a grey placeholder that pulses (still with simplified effects), for content that is loading |
| `StateMessage` | `kind` ("empty", "error", "offline"), `title`, `description` (`text` is the same), `retryVisible`, `retry()`; one look for empty, failed and offline screens |

Every interactive control takes keyboard focus (Tab, arrows in rows and
grids, Enter or Space to activate) and draws `FocusFrame` when focused by
keyboard. Every focusable control answers Enter (a gamepad's Select): a test
fails when a new one does not.

## TV mode and arrow navigation

For users: keys, gamepad buttons, the phone and the HDMI-CEC decision are
in [tv-mode.md](tv-mode.md).

`Theme.tvMode` (Settings > Interface > TV mode, the "TV mode" command of
Ctrl+K, or `--tv` for one run) changes at once, without a restart: body text
24 px (at 1920 by 1080 logical pixels), `controlHeight` and `minHitSize`
64 px, larger cards, icons and sidebar, a 4 px focus ring and halved
animations. Use the `Theme` sizes, not numbers, and a screen follows. With
`Theme.tvSafeArea` (Settings > Interface > Keep clear of the TV's edges, on
by default) the shell keeps `Theme.safeMargin` (5 %) free at each edge of
the pages that have its sidebar; the player and the sign-in page place
their controls themselves.

`SpatialNavigation` (`Jfd.Ui.Shell`, attached to `AppShell`) moves the focus
with the arrows nobody else took: the focused item, its `Keys` and
`KeyNavigation` (which always win), a `ListView` or `GridView` moving between
its delegates, and the page come first. It then focuses the nearest visible,
enabled item with `activeFocusOnTab` in that direction, the offset across
the direction weighted; a focus scope with focusable items inside is
entered. A view's `currentIndex` follows the focus, and a `Flickable` scrolls
to an item outside its visible part; delegates a view has not created are
reached through the view's own keys. PageUp and PageDown (a gamepad's LB
and RB) move the focus about a page, step by step through the containing
view's currentIndex (also `CardGrid` in TV mode, whose key navigation is
off), and never send keys to the focused control. A control whose own arrows would trap the focus
lists the arrows to give up in `navigationKeys` (`ChoiceBox` in TV mode);
those step through the containing view first (`step()`), so a row below
the view's edge is reached, then by geometry.
`CardGrid` in TV mode leaves the grid at its edges instead of wrapping.

The Menu key (a gamepad's View) opens the focused item's own menu where it
has one (`Keys.onMenuPressed`), otherwise it goes to the sidebar's entry of
the current page, and back. A new screen gets keyboard focus on its first
item and needs no `KeyNavigation` of its own.

On-screen keyboard: in TV mode a text field reached by an arrow, Tab or a
page key, or Select on a field, opens `OnScreenKeyboard` along the bottom
of the window, over dialogs and the command palette (`TextEntry`, in
`AppShell`). The field keeps the focus; the keyboard shows its label and
text (masked for a password) and takes the arrows, Select and Back while
open. English and Russian letters, symbols, digits, Shift, the cursor,
space, erase and Done (Enter in the field: the keyboard follows when that
moves the focus to the next field). A physical keyboard that types keeps it
away until the gamepad or the phone is used again. Text from the phone
(`SendString`) goes through `InputRouter::sendText` into the focused field.
Own implementation: Qt Virtual Keyboard is GPL-3.0. Keys, buttons and the
CEC decision: [tv-mode.md](tv-mode.md).

Sign-in in TV mode: on the password step Quick Connect, which needs no
typing, is the main button and has the focus, above the fields; Back goes
a step back (from the code to the password form, from there to the
servers). The address, name and password come from the on-screen keyboard
or the phone.

The player in TV mode: its display is a focus mode. Hidden, or with nothing
on it focused, Left and Right seek (held: 10, then 30, then 60 s steps
shown on the timeline, one seek when let go or when a gamepad's repeats
stop), Up and Down show it with the larger timeline focused (on a channel:
play/pause), Select plays or pauses; shown, the arrows move between its
controls. The menus are one row of named buttons, Episodes, Subtitles,
Audio, Settings; a menu opens with its first row focused and gives the
focus back to its button. Back closes the menu, then the display, then the
player. Page Up and Down (a gamepad's shoulders) stay the channel keys of
Live TV. The display keeps out of the outer 5 %.

Gamepads: `GamepadStatus` (a `Jfd.Ui.Shell` singleton) says whether one is
connected (the sidebar shows its sign and name, as does Settings > Gamepad)
and runs Settings > Gamepad > Check the buttons, where each press shows its
action instead of acting, until Back is pressed twice.

## Pages and navigation

Pages derive from `RoutePage` (`route`, `routeName`, `routeArgument`,
`title`). Routes: `login`, `home`, `library/<id>`, `collection/<id>`, `item/<id>`,
`search`, `player`, `group`, `settings`, and the routes other modules
register. From QML: `Navigator.navigate("item/" + id)`,
`Navigator.back()`, `Navigator.replace("home")`. `login` and `home` clear
the stack. Back is Esc, Backspace (outside text fields), Alt+Left and the
mouse back button; F11 toggles full screen. The sidebar is hidden on
`login` and `player`.

A module declares a route with a `RouteSpec`: the name, whether it takes an
argument (`album/<id>`; such routes may stack), whether it is a root, whether
the shell shows its sidebar and mini player around it (`chrome`), a page, and
optionally a sidebar entry (an icon name from `Icons` or a file URL, text,
order). `Navigator.setRouteAvailable(name, false)` hides the entry (and the
module's commands should follow) until the feature is configured:

```cpp
ui::RouteSpec music;
music.name = QStringLiteral("music");
music.hasSidebarEntry = true;
music.sidebar = {QStringLiteral("music"), QCoreApplication::translate("Music", "Music"), nullptr, 400};
services.navigator->registerRoute(music);
services.navigator->registerPage(QStringLiteral("music"), QUrl(...));
```

The shell has a strip under the pages for the mini player,
`shell.miniPlayerSlot`: a module puts its item in it (`parent:
shell.miniPlayerSlot`); the strip is as tall as the item and shows wherever
the sidebar shows.

`CommandRegistry` (a QML singleton, like `Navigator`) holds the commands of
the Ctrl+K palette: an id, an already translated title, an optional
availability test and the action. `library::HomeSections` (no Qt Quick, in
`jfd/library/HomeSection.h`) holds the rows of the home screen the same
way; each module registers its own rows and the user's order applies to all.

A module registers its pages in its install function in `src/desktop`:

```cpp
void installLibrary(AppServices& services, QQmlEngine& engine) {
  services.navigator->registerPage(QStringLiteral("home"),
      QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Library/HomePage.qml")));
}
```

and adds one line calling it in `src/desktop/main.cpp`, plus its QML
module and plugin to `src/desktop/CMakeLists.txt` and a
`Q_IMPORT_QML_PLUGIN` line. Routes without a page show a placeholder.

To show a message: `shell.showError(text)` through the window's `AppShell`
(objectName `shell`).

## Library screens

`Jfd.Ui.Library` holds the `login`, `home`, `library/<id>`,
`collection/<id>`, `item/<id>` and `search` pages. The server connection
belongs to `account::Account` (`src/account`, created by `installLibrary` in
`src/desktop`): the `JellyfinClient` and `SessionSocket` in
`AppServices::client` and `socket` are its, and every module that keeps
per-user state registers it once with `AppServices::account->scope()`
(`account::UserScope`, ordered: library, session, downloads, group, music,
player). `ui::LibraryScreens` builds the screens on it. Once the scope has
begun, `home` opens; once it has ended (sign-out or a rejected token),
`login`.

From QML, `Library` gives `signIn`, `home`, `search`,
`createLibrary(id, page)`, `createDetails(id, page)`, `image(url)` and
`routeFor(id, type)`. Cards load images through `image://jfd/`, which
decodes at the card's `sourceSize` and keeps what Qt Quick holds under a
video memory budget (192 MiB by default; over it, new images come at half
size). Play on the item page calls the handler set with
`AppServices::libraryUi->setPlayHandler(...)`; without one the page says the
feature is not available yet. In a room the handler starts the item for the
room (there is no separate Watch together button: rooms are made and joined
on the `group` page only).

Libraries remember their sort, watched state, favorites, genres (several at
once; an item matches any of them), year and, for movie libraries, whether
they list movies or collections. `Library.createLibrary` applies what the
library had last time and keeps every change, through the `library/filters`
setting (a JSON object keyed by library id). A collection (BoxSet) has its
own page, `collection/<id>`, reached from a card in the collections list or
from the "In collections" row of a movie's page. Jellyfin lists a
collection's items but not an item's collections, so `CollectionIndex`
lists every collection once, remembers who is in which, and builds again
after a library change.

Sign-in is a glass card on a gradient lit by the accent. Once a server
answers, `SignIn.publicUsers` (from `GET Users/Public`, not waited for) puts
the server's users above the fields ("Who's watching?"): a user fills the
name and moves to the password, "Other" clears the name. A server that hides
its users, or does not answer, shows the plain form. "Change" on the server
line goes back to the servers. TV mode keeps Quick Connect first.

Search has a large field and tabs by kind (All, Films, Series, Episodes,
People, Music) with how many of each the latest answer holds
(`SearchModel.kind`, `SearchModel.counts`). A tab filters the answer
already there; nothing is asked again, and empty kinds have no tab.

### Home sections and recommendations

Every row of the home screen is a `library::HomeSection` (id, translated
title, card shape, default order, a model, an optional `reload`). The
built-in rows (`resume`, `nextUp`, `latest/<library>`) and the ones other
modules add through `HomeModel::sections()->add(...)` ("music.recent") are treated alike. The user's arrangement is the hidden
setting `appearance/homeSections`, a JSON array of ids with `-` in front of
the rows switched off (`["resume","-nextUp"]`); ids that are not registered
are skipped and new sections come after the arranged ones, so modules can
come and go. "Customize home" (button on the home page, a setting and a
Ctrl+K command) edits it with `HomeModel::moveSection`,
`setSectionVisible` and `resetArrangement`.

`library::RecommendationRows` adds the recommendation rows: the server's
`Movies/Recommendations` ("Because you watched ...", "Directed by ..."),
"Because you watched <series>" from `Items/{id}/Similar` for the last
watched series, and "Picks" from `Items/Suggestions`. It is best effort: a
failed or slow request (8 s) leaves its rows out, empty rows are not shown,
and answers are kept for 5 minutes.

### Player header, bar, menus and episodes

The player's top bar has a round Back, the title (for an episode the series,
with "Season 1 · Episode 3 «Name»" under it, from the current entry of
`PlayerSession.episodes`), the room pill in a SyncPlay group (`GroupOsd`:
initials, status dot, room name and viewer count) and the time now with
"Ends at HH:MM" (the time left at the current speed added to the clock, in the
system's short format; the clock can be turned off).

The bottom bar: the timeline (chapter marks, segment bands, trickplay preview
with the chapter's name), then one row of icons. On the left: the round light
play/pause, previous and next episode, a step back and forward (the circling
arrows with the step's seconds inside; Settings, Playback, Seek step, 15 s by
default, which the arrow keys and the gamepad's triggers use too), volume,
"elapsed / length", and the part playing: the segment, or the chapter between
‹ and › that jump to the chapter before (from well into one, its start) and
after. On the right: audio and subtitles, the speed (its icon, with "1.5×"
beside it when not normal), segments, episodes, the player settings (gear),
picture in picture and fullscreen. The menus open 12 px above the bar. Skip
intro/credits sits above the bar on the right.

- **TracksMenu**: audio and subtitles side by side (one column in a narrow
  window), and a row that opens the settings at the subtitles.
- **TuneMenu** (gear): rows with their values (quality, speed, subtitles' look
  and delay, audio delay), the automatic skip switch and Stats for nerds. A
  row opens its choices in place under a header with Back. Subtitle size and
  position are `PlayerSession.subtitleSize` / `subtitlePlacement`, the same
  values as the `subtitles/size` and `subtitles/placement` settings (the
  application keeps them in step).
- **EpisodesSheet** (Episodes button, key E): a sheet along the bottom in
  place of the bar, with season tabs (Page Up/Down, a gamepad's LB/RB) and a
  strip of episode cards (number or "Now"/"Next", watched check, progress,
  run time and overview). Choosing one plays it with `playEpisode`; in a
  group it goes to the group's queue.

## Music screens

`src/ui/music` (module `Jfd.Ui.Music`) is the face of the queue and player in
`src/music`. Routes: `music` (the library page with Recent, Albums, Artists,
Playlists, Genres and Songs), `album/<id>`, `artist/<id>`, `playlist/<id>`,
`genre/<name>` and `nowplaying`. A library of type music opens `music` from the
sidebar and the library page.

- **Lists.** `MusicListModel` (`Music.createList(kind, owner)`) pages like the
  library models: every row has its place from the first answer and pages load
  as rows come into view, so 10,000 albums scroll at once. Kinds: albums,
  artists, songs, playlists, genres, albumTracks, playlistTracks,
  recentlyPlayed. `removeEntry(row)` takes a track out of a playlist.
- **Player.** `MusicSession` is the one `MusicPlayer`; QML never talks to the
  session. The mini-player is a strip in the shell's `miniPlayerSlot`
  (`installMiniPlayer`): cover, title, controls, progress and, on a window of
  760 px or more, the volume. The shell hides it in the video player.
- **Guards.** `PlayGuard` asks before a track replaces a movie that is playing;
  `GroupHint` replaces the buttons in a SyncPlay group (music is not available
  there). Starting a video stops the music and keeps the queue. A track
  found by the global search plays through `Library.playTrack` (its album
  as the queue); the window asks first while a video plays.
- **Lyrics.** "Now playing" shows the track's lyrics in place of the queue
  (`MusicLyrics`, `Music.createLyrics(owner)`, from `GET Audio/{id}/Lyrics`).
  Synced lines follow the position: the line sung now is bold and kept near
  the middle, a click on a line seeks to it. Plain lyrics are shown as text;
  a track without any says "No lyrics".
- **Strings and icons.** Strings are `qsTr` with a Russian file in
  `src/ui/music/i18n`; icons are SVG files in `src/ui/music/icons` through
  `MusicIcons`.

## Settings from QML

`AppSettings.value("interface/language")`, `AppSettings.setValue(key,
value)`, `AppSettings.trigger(key)` for actions. Read `AppSettings.revision`
in a binding so it re-evaluates when values change. See
[settings.md](settings.md) for registering a section.

The settings page lists the sections on the left (`SettingsSearch.sections`,
under the search field; hidden below 900 px) and the settings on the right,
one card per section. A section on the left scrolls to its card, and the
section whose card is at the top is marked.

## Tests

`jfd_ui_tv_tests` checks `SpatialNavigation` on synthetic screens (rows of
different length, a list, a scrolled pane, a grid of 200 mostly uncreated
delegates; every item reaches every other, Back always gets out) and on the
shell at 1920 by 1080 in TV mode (safe area, Menu, Back), Enter on every
focusable control, the TV sizes, and switching TV mode 20 times on 1000
cards (each switch at most 100 ms, memory at most 5 MiB more), and the
on-screen keyboard (opening, every layout, Done to the next field, a
physical keyboard keeping it away, text from the phone).
`jfd_ui_login_tv_tests` signs in on a fake server in TV mode with arrows,
Select and Back alone: every control of each step reached, none trapping
the focus, the address and the password typed on the on-screen keyboard,
Quick Connect first, and Back between the steps.
`jfd_ui_gamepad_tests` drives the shell in TV mode with SDL's virtual
gamepad alone.
`jfd_ui_rooms_tests` drives the rooms tab on a fake server with the keyboard
alone and in TV mode (arrows, Select, Back, the room's name on the on-screen
keyboard), checks that a poll keeps the cards of unchanged rooms, the empty,
offline and access states, and the room card's contrast and focus ring in
both themes; with `JFD_UI_SCREENSHOTS` it saves the tab in both themes at
four window sizes.

`desktop_smoke` starts the application with a temporary profile, checks the
login page, visits every route and back, and fails if shutdown takes a
second or more. Set `JFD_SMOKE_SCREENSHOTS=<dir>` to save a screenshot of
each page.

`scripts/measure/library-bench.sh build/tests/jfd_library_bench` times the
library screens against the test server (the `JFD_TEST_*` variables) and
fails when a threshold is missed: cold start to home under 4 s, warm start
under 2 s, and a series, its season and home again under 1 s each. It also
scrolls a 1000-item grid on an in-process fake server and reports the frame
intervals; on Xvfb that number is only a preliminary for the laptop
threshold. The Library screens workflow runs it under Xvfb and puts the
table in the job summary.

## Picture in picture

The player page has a PiP button (key P). The video moves to a small frameless
window that stays on top (`PipWindow.qml`, created once by `Main.qml`); the
main window goes back to the library and the file keeps playing. Controls show
on hover: play/pause and back to the player page. Space pauses, Esc, the back
button or closing the window return to the player page. Drag anywhere to move,
the corner to resize. Not offered with HDR10 output. On Wayland the window
cannot be forced on top or placed: it opens where the compositor puts it, a
hint says to use the window menu's Always on top, and the title is fixed
(`Jellyfin Desktop Next - Picture in picture`) for compositor rules. Details:
`docs/pip.md`.

## Scrolling, item pages and episodes

- The plain mouse wheel always scrolls the page, also over a card row; a row
  scrolls sideways with Shift + wheel, a touchpad swipe, dragging or the arrow
  keys (`CardRow`).
- The item page's backdrop is part of the scrolling content: it sits at the top
  and scrolls away, fading into the page background with a gradient.
- An episode has no page in normal use: clicking an episode card (home, search,
  latest) or an episode row on a series or season page plays it, resuming where
  it stopped. The series page lists the episodes with their stills and
  overviews.
- In the player, the audio, subtitle and settings menus open just above the
  button row. A tooltip that would not fit below its button shows above it;
  tooltips are drawn on the window's content item and kept inside the
  window, so clipping lists cannot cut them. Only one shows at a time: the
  first after a short wait, the next one along a row at once, and a tooltip
  that goes hides without a fade.
- The player's bar buttons and timeline take focus only from Tab: after a
  click, Space and the arrows are still the player's keys. A single click on
  the video waits for the double-click time (a double-click only toggles
  fullscreen); a click while the bar is hidden only shows it.

### Wheel scrolling

`SmoothWheel {}` declared inside a `Flickable`, `ListView` or `GridView`
makes the plain vertical wheel scroll it with an eased animation and a fixed
step of 130 px per notch (quick notches add up); touchpads, which send exact
pixel deltas, move it one to one. Shift + wheel and sideways swipes stay with
the view. Every page's main scrolling view has one; add it to a new page's.

## Live TV

- `livetv`: the channel list with what is on now and next; Guide opens the
  grid. Its sidebar entry shows only when `LiveTv/Info` has Live TV for the
  user, and so does the home screen's "On now" row (section `livetv.onNow`,
  a card per channel with a programme on; a card tunes in).
- `guide` (`GuidePage`, model `LiveTvGuide`): a row per channel and the
  programmes along a time line from the half hour before now, 24 hours on.
  Only the rows on screen are made, and in a row only the programmes in view
  and an hour either side; the guide loads in six-hour chunks as the view
  reaches them. Arrows move between programmes (Up and Down keep the time),
  Page Up and Page Down (a gamepad's LB and RB) a screen of time, Home back
  to now; Enter on what is on the air tunes in, on a later programme it
  opens the channel. Shift + wheel or a sideways swipe moves along time.
- `channel/<id>` (`ChannelPage`, model `LiveTvSchedule`): the channel, Watch
  (focused) and its programmes from now.
- In the player a channel has no timeline, no "Ends at" and no seeking; the
  header names the channel and the programme on now with its times and
  progress. Page Up / Channel Up / N (and LB) go to the next channel, Page
  Down / Channel Down (RB) to the one before, round the server's list. A
  channel always plays on this device, also in a group.
- Recording (the server's own DVR; `Library.recorder`, model
  `LiveTvRecorder`) is offered only when `LiveTv/Info` has Live TV for the
  user and the user may manage it (`Policy.EnableLiveTvManagement`, or an
  administrator; the server refuses timers to anyone else). Then, on the
  guide, the Menu key (a gamepad's View button) opens the focused
  programme's actions (Record, Record series for a series, Cancel recording,
  Cancel series recording), R records it or cancels its recording, and the
  same actions are buttons under the programme's details; on a channel's
  page the focused programme has a Record (or Cancel recording) button,
  reached with Right, and the Menu key and R work the same. A red dot marks
  a programme the server will record, a ring one of a series recording.
- `recordings` (`RecordingsPage`, models `LiveTvRecordings` and the
  recorder), from the Live TV screen and the guide: the recordings, the
  one recording now first, then the schedule (series recordings, then the
  recordings to come by start). A finished recording plays as an item, with
  its time line, from where it was left; Delete (its button or the Delete
  key) removes it and its file after a question (`DELETE Items/{id}`: 12.2's
  `DELETE LiveTv/Recordings/{id}` leaves the file, which the next scan of the
  Recordings folder brings back). One recording now has no length yet (the
  server streams it as a live source): its button stops it, after a
  question, and what it recorded stays. A schedule row's button (Enter on
  the row reaches it) cancels it.

## Watching together: rooms

The route `group` (sidebar "Watch together"; the tab keeps that name until
the owner decides on "Rooms") is `GroupPage`, which shows one of two pages:

- `RoomsPage` outside a room: the server's rooms (SyncPlay groups) as
  `RoomCard`s, Create room (a name only), and a room's card before joining:
  its state and members, Join and Back to rooms. Esc or Back closes an open
  card before it leaves the page. "No rooms right now" when the list is
  empty (the text does not promise every room of the server: 12.2 hides
  rooms whose queue holds something the user may not see), "No connection"
  while `Library.online` is false, a refusal from the server in words above
  the list.
- `RoomPage` in a room: a panel with the state, the room's name, what plays
  and how well we keep up, the speed for everyone as text (anyone changes it
  in the player), the members as initials, our own delay to the server, and
  Open the player, Next and Leave room; the queue under it.

Whenever the room moves to an item (our Play, another member's, Next),
`GroupModel::itemOpening` fires and `installGroup` shows the player (not
over the picture-in-picture window). The server shares no other member's
delay or position with a non-admin (`GroupInfoDto` has names only, and
`GET /Sessions` returns only the user's own sessions), so the room shows
only ours.

Both read the session's `GroupModel` (QML `GroupSession`) and `RoomsModel`
(QML `RoomsSession`, `src/session`). `RoomsModel` is the list: it asks for
`SyncPlay/List` at once when the page shows in the active window
(`RoomsSession.active`, which the page binds to its visibility and
`Window.active`) and then every 5 s, never while hidden, offline or without
the right to join; each answer is compared with the rows, so an unchanged
room keeps its card (no model signal at all) and a changed one is updated in
place. Joining, creating and leaving go through `GroupModel`.

The user's `SyncPlayAccess` (from `Users/Me`, asked again after each sign-in):

| Access | What the app offers |
| --- | --- |
| `CreateAndJoinGroups` (or unknown) | everything |
| `JoinGroups` | the rooms and Join; no Create room |
| `None` | no tab: `Navigator.setRouteClosed("group")` hides the entry, drops Ctrl+K's "Open Watch together" and refuses the route from anywhere (a command from a second start, the phone); the phone's Play in a group plays nothing in one |

On stock 12.2 a room has only a name, a state and its members' names, so the
card shows those; marks for a member limit, a password or an invitation, and
what is playing, wait for the rooms extension of the server fork (package
3.9, steps 3 and 4).

