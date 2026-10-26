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
| `Jfd.Ui.Player` | `src/ui/player` | player branch | player page, OSD, track menus |
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
| `Icon`, `Icons` | `Icon { source: Icons.play; size: 22 }`; icons: home, search, group, settings, back, close, play, check, error, library, menu |
| `InputField` | `label`, `text`, `placeholderText`, `errorText`, `password`, `accepted()` |
| `ToggleSwitch` | `checked`, `toggled()` |
| `ChoiceBox` | `model: [{ value, label }]`, `currentValue`, `indexOfValue()`, `activated()` |
| `ValueSlider` | `from`, `to`, `stepSize`, `value`, `moved()` |
| `PosterCard` | `title`, `subtitle`, `imageSource`, `shape` ("poster", "thumb", "square"), `progress` (0..1), `played`, `unplayedCount`, `cardWidth`, `clicked()` |
| `CardRow` | `title`, `model`, `delegate`, `cardHeight`, `currentIndex`, `view` |
| `CardGrid` | `model`, `delegate`, `cardWidth`, `cardHeight`, `currentIndex`, `view` |
| `ListItem` | `text`, `subtitle`, `trailingText`, `iconSource`, `highlighted`, `clicked()` |
| `SectionHeader`, `Heading` | text styles for a group title and a page title |
| `AppDialog` | `title`, `text`, `acceptText`, `rejectText`, `open()`, `accepted()`, `rejected()` |
| `Toast` | `show(text, kind)` with kind "info" or "error"; use the shell's |
| `LoadingIndicator` | `running` |
| `FocusFrame` | the keyboard focus ring; `shown: control.visualFocus` |

Every interactive control takes keyboard focus (Tab, arrows in rows and
grids, Enter or Space to activate) and draws `FocusFrame` when focused by
keyboard.

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
`collection/<id>`, `item/<id>` and `search` pages. `ui::LibraryScreens` (installed by `installLibrary` in
`src/desktop`) owns the server connection: the `JellyfinClient` and
`SessionSocket` in `AppServices::client` and `socket` are the ones the
session and group modules should use. Signing in opens `home`, signing out
or a rejected token opens `login`.

From QML, `Library` gives `signIn`, `home`, `search`,
`createLibrary(id, page)`, `createDetails(id, page)`, `image(url)` and
`routeFor(id, type)`. Cards load images through `image://jfd/`, which
decodes at the card's `sourceSize` and keeps what Qt Quick holds under a
video memory budget (192 MiB by default; over it, new images come at half
size). Play and Watch together on the item page call the handlers set with
`AppServices::libraryUi->setPlayHandler(...)` and
`setWatchTogetherHandler(...)`; without one the page says the feature is not
available yet.

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

### Home sections and recommendations

Every row of the home screen is a `library::HomeSection` (id, translated
title, card shape, default order, a model, an optional `reload`). The
built-in rows (`resume`, `nextUp`, `latest/<library>`) and the ones other
modules add through `HomeModel::sections()->add(...)` ("music.recent",
"seerr.popular") are treated alike. The user's arrangement is the hidden
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

## Settings from QML

`AppSettings.value("interface/language")`, `AppSettings.setValue(key,
value)`, `AppSettings.trigger(key)` for actions. Read `AppSettings.revision`
in a binding so it re-evaluates when values change. See
[settings.md](settings.md) for registering a section.

## Tests

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
