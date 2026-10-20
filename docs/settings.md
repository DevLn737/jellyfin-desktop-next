# Settings

Module `settings` (`src/settings`, Qt Core only). Every other module may use it.

## Registering a section

Each module describes its own section and registers it at startup, before
`Settings` opens the file:

```cpp
#include <jfd/settings/SettingsRegistry.h>

namespace jfd::playback {
  bool registerSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("playback");
    section.context = "PlaybackSettings";   // translation context of the labels
    section.title = QT_TRANSLATE_NOOP("PlaybackSettings", "Playback");
    section.order = 100;

    settings::SettingDescriptor hwdec;
    hwdec.key = QStringLiteral("hardwareDecoding");
    hwdec.type = settings::SettingType::Choice;
    hwdec.defaultValue = QStringLiteral("auto");
    hwdec.label = QT_TRANSLATE_NOOP("PlaybackSettings", "Hardware decoding");
    hwdec.choices = {{QStringLiteral("auto"), QT_TRANSLATE_NOOP("PlaybackSettings", "Automatic")},
                     {QStringLiteral("off"), QT_TRANSLATE_NOOP("PlaybackSettings", "Off")}};
    hwdec.legacyKey = QStringLiteral("video.hardwareDecoding");
    section.settings.append(hwdec);
    return registry.registerSection(std::move(section));
  }
}
```

Labels stay untranslated English in the descriptor, marked with
`QT_TRANSLATE_NOOP` so `lupdate` puts them in the owning module's `.ts` file;
the settings screen translates them with the section's `context`.

Types: `Bool`, `Int`, `Double` (with `minimum`/`maximum`/`step`), `String`,
`Choice` and `Action` (a button with no stored value; connect to
`Settings::actionTriggered`). `hidden` keeps state such as window geometry
off the settings screen. `legacyKey` names the 2.x client's key
(`resources/settings/settings_description.json`, `section.value`) for the
import planned in stage 2b.

Section orders in use: account 0, interface 10, diagnostics 1000. Leave gaps.

## Reading and writing

`Settings` takes full keys, `"<section>/<key>"`. Reads return the default
for anything not stored or no longer valid; writes are validated and saved
immediately, and `valueChanged` fires only on a real change. `Settings` is a
`QObject` with invokable `value`, `setValue`, `reset` and `trigger`, so QML
can use it directly.

The file is `settings.ini` in the per-user configuration folder
(`Settings::defaultFilePath()`).

## Changing a section's schema

When a stored value changes meaning, is renamed or removed, raise the
section's `version` and add a `Migration` from the previous version. The
registry refuses a section whose migrations do not cover every version from
1 to the current one. Migrations see the section's raw stored values as a
`QVariantMap` and run once, in order, when `Settings` opens the file.
Sections and keys the build does not know are left untouched, and a section
written by a newer build is neither migrated nor downgraded.
