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
`easing`. Durations are 0 when `Theme.reduceMotion` is on (the "Reduce
animations" setting), so always animate with them. The interface scale
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
`title`). Routes: `login`, `home`, `library/<id>`, `item/<id>`, `search`,
`player`, `group`, `settings`. From QML: `Navigator.navigate("item/" + id)`,
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
