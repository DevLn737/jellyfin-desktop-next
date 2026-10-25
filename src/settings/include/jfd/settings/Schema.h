#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <functional>

// Settings are described, not hand-written: each module registers a section
// with typed settings, defaults and a schema version. The settings screen is
// built from these descriptions and the store validates every write against
// them.
//
// Labels are untranslated English. Each section names the translation
// context its labels are marked in (QT_TRANSLATE_NOOP(context, "...")), so
// lupdate finds them in the owning module and the screen translates them with
// QCoreApplication::translate(context, label).
namespace jfd::settings {

  enum class SettingType {
    Bool,
    Int,
    Double,
    String,
    // One of `choices`; stored as the choice's value string.
    Choice,
    // A button on the settings screen (for example "Open log folder"); has no
    // stored value. The owner connects to Settings::actionTriggered.
    Action,
  };

  struct Choice {
    QString value;
    const char* label = nullptr;
  };

  struct SettingDescriptor {
    // Unique within the section; the full key is "<section>/<key>".
    QString key;
    SettingType type = SettingType::Bool;
    QVariant defaultValue;
    const char* label = nullptr;
    const char* description = nullptr;
    QList<Choice> choices;
    // Int and Double only; inclusive. Equal bounds mean unbounded.
    double minimum = 0;
    double maximum = 0;
    double step = 1;
    // Unit shown after the value, for example "%" or "ms"; translated in the
    // section's context like the label, so mark it with QT_TRANSLATE_NOOP.
    const char* unit = nullptr;
    // The key of the same setting in the 2.x client
    // (resources/settings/settings_description.json, "section.value"), for
    // the import planned in stage 2b. Empty when 2.x had none.
    QString legacyKey;
    // Not shown on the settings screen (window geometry and other state).
    bool hidden = false;
    // The change applies after a restart; the screen says so.
    bool requiresRestart = false;
  };

  // Rewrites a section's stored values from one schema version to the next.
  // Keys are those inside the section ("language", not "interface/language").
  struct Migration {
    int fromVersion = 0;
    std::function<void(QVariantMap& values)> apply;
  };

  struct SectionDescriptor {
    QString id;
    // Translation context of the labels in this section.
    const char* context = nullptr;
    const char* title = nullptr;
    // Position on the settings screen; lower comes first.
    int order = 0;
    // Starts at 1. Raise it and add a Migration from the previous version
    // whenever stored values change meaning, are renamed or removed.
    int version = 1;
    QList<SettingDescriptor> settings;
    QList<Migration> migrations;
    // Not shown on the settings screen.
    bool hidden = false;
  };

  // A value converted to the setting's type, or an invalid QVariant when it
  // cannot be (wrong type, out of range, unknown choice).
  QVariant coerce(const SettingDescriptor& setting, const QVariant& value);

} // namespace jfd::settings
