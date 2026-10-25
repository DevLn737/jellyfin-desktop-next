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
`posterAspect`, `thumbAspect`; `durationFast` / `Normal` / `Slow` and
`easing`. Durations are halved when `Theme.reducedEffects` is on (the
"Simplified effects" setting, automatically on with integrated or software
graphics), so always animate with them; blur only when `Theme.blurEnabled`. The interface scale
setting applies through `QT_SCALE_FACTOR`; do not scale sizes by hand.

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
`search`, `player`, `group`, `settings`. From QML: `Navigator.navigate("item/" + id)`,
`Navigator.back()`, `Navigator.replace("home")`. `login` and `home` clear
the stack. Back is Esc, Backspace (outside text fields), Alt+Left and the
mouse back button; F11 toggles full screen. The sidebar is hidden on
`login` and `player`.

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
