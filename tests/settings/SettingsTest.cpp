#include "../support/DoctestQt.h"

#include <jfd/settings/CommonSections.h>
#include <jfd/settings/Settings.h>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace jfd::settings;

namespace {

  SettingDescriptor boolSetting(const char* key, bool defaultValue) {
    SettingDescriptor setting;
    setting.key = QString::fromLatin1(key);
    setting.type = SettingType::Bool;
    setting.defaultValue = defaultValue;
    setting.label = key;
    return setting;
  }

  SectionDescriptor testSection(int version = 1) {
    SectionDescriptor section;
    section.id = QStringLiteral("test");
    section.context = "Test";
    section.title = "Test";
    section.version = version;
    section.settings.append(boolSetting("enabled", true));

    SettingDescriptor volume;
    volume.key = QStringLiteral("volume");
    volume.type = SettingType::Int;
    volume.defaultValue = 100;
    volume.label = "Volume";
    volume.minimum = 0;
    volume.maximum = 130;
    section.settings.append(volume);

    SettingDescriptor mode;
    mode.key = QStringLiteral("mode");
    mode.type = SettingType::Choice;
    mode.defaultValue = QStringLiteral("auto");
    mode.label = "Mode";
    mode.choices = {{QStringLiteral("auto"), "Auto"}, {QStringLiteral("off"), "Off"}};
    section.settings.append(mode);

    SettingDescriptor action;
    action.key = QStringLiteral("run");
    action.type = SettingType::Action;
    action.label = "Run";
    section.settings.append(action);
    return section;
  }

  QString iniPath(const QTemporaryDir& dir) { return dir.filePath(QStringLiteral("settings.ini")); }

} // namespace

TEST_CASE("Values default, validate, persist and notify") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  SettingsRegistry registry;
  REQUIRE(registry.registerSection(testSection()));
  {
    Settings settings(registry, iniPath(dir));
    QSignalSpy changed(&settings, &Settings::valueChanged);
    CHECK(settings.value(QStringLiteral("test/enabled")) == QVariant(true));
    CHECK(settings.value(QStringLiteral("test/volume")) == QVariant(100));
    CHECK(settings.isDefault(QStringLiteral("test/volume")));
    CHECK_FALSE(settings.value(QStringLiteral("test/missing")).isValid());
    CHECK_FALSE(settings.value(QStringLiteral("test/run")).isValid());

    CHECK(settings.setValue(QStringLiteral("test/volume"), 80));
    CHECK(settings.setValue(QStringLiteral("test/volume"), 80));
    CHECK(settings.setValue(QStringLiteral("test/enabled"), false));
    CHECK(settings.setValue(QStringLiteral("test/mode"), QStringLiteral("off")));
    CHECK(changed.count() == 3);
    CHECK(changed.at(0).at(0).toString() == QStringLiteral("test/volume"));

    CHECK_FALSE(settings.setValue(QStringLiteral("test/volume"), 131));
    CHECK_FALSE(settings.setValue(QStringLiteral("test/volume"), 12.5));
    CHECK_FALSE(settings.setValue(QStringLiteral("test/mode"), QStringLiteral("sometimes")));
    CHECK_FALSE(settings.setValue(QStringLiteral("test/enabled"), QStringLiteral("maybe")));
    CHECK_FALSE(settings.setValue(QStringLiteral("test/missing"), 1));
    CHECK(changed.count() == 3);
  }
  // A restart reads what was written, with the declared types.
  Settings reopened(registry, iniPath(dir));
  CHECK(reopened.value(QStringLiteral("test/volume")) == QVariant(80));
  CHECK(reopened.value(QStringLiteral("test/enabled")) == QVariant(false));
  CHECK(reopened.value(QStringLiteral("test/mode")) == QVariant(QStringLiteral("off")));

  QSignalSpy changed(&reopened, &Settings::valueChanged);
  reopened.reset(QStringLiteral("test/volume"));
  CHECK(reopened.value(QStringLiteral("test/volume")) == QVariant(100));
  CHECK(changed.count() == 1);
}

TEST_CASE("Hand-edited or out-of-range values fall back to the default") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  {
    QFile file(iniPath(dir));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("[schema]\ntest=1\n[test]\nvolume=900\nenabled=yes please\nmode=off\n");
  }
  SettingsRegistry registry;
  REQUIRE(registry.registerSection(testSection()));
  Settings settings(registry, iniPath(dir));
  CHECK(settings.value(QStringLiteral("test/volume")) == QVariant(100));
  CHECK(settings.value(QStringLiteral("test/enabled")) == QVariant(true));
  CHECK(settings.value(QStringLiteral("test/mode")) == QVariant(QStringLiteral("off")));
}

TEST_CASE("Actions are triggered, not stored") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  SettingsRegistry registry;
  REQUIRE(registry.registerSection(testSection()));
  Settings settings(registry, iniPath(dir));
  QSignalSpy triggered(&settings, &Settings::actionTriggered);
  CHECK_FALSE(settings.setValue(QStringLiteral("test/run"), true));
  settings.trigger(QStringLiteral("test/run"));
  settings.trigger(QStringLiteral("test/volume"));
  REQUIRE(triggered.count() == 1);
  CHECK(triggered.at(0).at(0).toString() == QStringLiteral("test/run"));
}

TEST_CASE("Migrations run in order from the stored version and keep unknown keys") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  {
    // Version 1 called the volume "level", in percent of 200.
    QFile file(iniPath(dir));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("[schema]\ntest=1\nfuture=7\n[test]\nlevel=40\nenabled=false\n[future]\nx=1\n");
  }
  SettingsRegistry registry;
  SectionDescriptor section = testSection(3);
  QStringList applied;
  section.migrations = {
      {2,
       [&](QVariantMap& values) {
         applied << QStringLiteral("2->3");
         values.insert(QStringLiteral("volume"), values.value(QStringLiteral("volume")).toInt() + 1);
       }},
      {1,
       [&](QVariantMap& values) {
         applied << QStringLiteral("1->2");
         values.insert(QStringLiteral("volume"), values.take(QStringLiteral("level")).toInt() * 2);
       }},
  };
  REQUIRE(registry.registerSection(section));
  {
    Settings settings(registry, iniPath(dir));
    CHECK(applied == QStringList{QStringLiteral("1->2"), QStringLiteral("2->3")});
    CHECK(settings.storedVersion(QStringLiteral("test")) == 3);
    CHECK(settings.value(QStringLiteral("test/volume")) == QVariant(81));
    CHECK(settings.value(QStringLiteral("test/enabled")) == QVariant(false));
  }
  // Opening again does not migrate twice.
  applied.clear();
  Settings again(registry, iniPath(dir));
  CHECK(applied.isEmpty());
  CHECK(again.value(QStringLiteral("test/volume")) == QVariant(81));

  // A section this build does not know survives untouched.
  QSettings raw(iniPath(dir), QSettings::IniFormat);
  CHECK(raw.value(QStringLiteral("future/x")).toInt() == 1);
  CHECK(raw.value(QStringLiteral("schema/future")).toInt() == 7);
  CHECK_FALSE(raw.contains(QStringLiteral("test/level")));
}

TEST_CASE("A section from a newer build is not migrated or downgraded") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  {
    QFile file(iniPath(dir));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("[schema]\ntest=5\n[test]\nvolume=50\nrenamed=1\n");
  }
  SettingsRegistry registry;
  REQUIRE(registry.registerSection(testSection(1)));
  Settings settings(registry, iniPath(dir));
  CHECK(settings.storedVersion(QStringLiteral("test")) == 5);
  CHECK(settings.value(QStringLiteral("test/volume")) == QVariant(50));
}

TEST_CASE("A new file starts every section at its current version") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  SettingsRegistry registry;
  SectionDescriptor section = testSection(2);
  bool migrated = false;
  section.migrations = {{1, [&](QVariantMap&) { migrated = true; }}};
  REQUIRE(registry.registerSection(section));
  Settings settings(registry, iniPath(dir));
  CHECK_FALSE(migrated);
  CHECK(settings.storedVersion(QStringLiteral("test")) == 2);
}

TEST_CASE("The registry rejects inconsistent sections") {
  SettingsRegistry registry;
  REQUIRE(registry.registerSection(testSection()));
  CHECK_FALSE(registry.registerSection(testSection()));

  SectionDescriptor wrongDefault = testSection();
  wrongDefault.id = QStringLiteral("other");
  wrongDefault.settings[1].defaultValue = 500;
  CHECK_FALSE(registry.registerSection(wrongDefault));

  SectionDescriptor duplicateKey = testSection();
  duplicateKey.id = QStringLiteral("other");
  duplicateKey.settings.append(boolSetting("enabled", false));
  CHECK_FALSE(registry.registerSection(duplicateKey));

  SectionDescriptor missingMigration = testSection(3);
  missingMigration.id = QStringLiteral("other");
  missingMigration.migrations = {{1, [](QVariantMap&) {}}};
  CHECK_FALSE(registry.registerSection(missingMigration));

  SectionDescriptor reserved = testSection();
  reserved.id = QStringLiteral("schema");
  CHECK_FALSE(registry.registerSection(reserved));

  CHECK(registry.sections().size() == 1);
  CHECK(registry.setting(QStringLiteral("test/mode")) != nullptr);
  CHECK(registry.setting(QStringLiteral("test")) == nullptr);
}

TEST_CASE("The common sections register and keep their keys") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  SettingsRegistry registry;
  REQUIRE(registerCommonSections(registry));
  CHECK_FALSE(registerCommonSections(registry));
  Settings settings(registry, iniPath(dir));
  CHECK(settings.value(keys::Language) == QVariant(QStringLiteral("system")));
  CHECK(settings.value(keys::Scale) == QVariant(1.0));
  CHECK(settings.setValue(keys::Scale, 1.5));
  CHECK_FALSE(settings.setValue(keys::Scale, 3.0));
  CHECK(settings.value(keys::LogLevel) == QVariant(QStringLiteral("info")));
  CHECK(registry.setting(keys::LogLevel)->legacyKey == QStringLiteral("main.logLevel"));
  CHECK(registry.section(QStringLiteral("window"))->hidden);
  // Sections come in screen order.
  const auto sections = registry.sections();
  CHECK(sections.first().id == QStringLiteral("account"));
  CHECK(sections.last().id == QStringLiteral("diagnostics"));
}

TEST_CASE("The old Reduce animations switch becomes Simplified effects") {
  for (const auto& [stored, expected] :
       {std::pair{"true", "on"}, std::pair{"false", "auto"}, std::pair{"", "auto"}}) {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
      QFile file(iniPath(dir));
      REQUIRE(file.open(QIODevice::WriteOnly));
      QByteArray text("[schema]\ninterface=1\n[interface]\nscale=1.25\n");
      if (*stored != '\0') text += QByteArray("reduceMotion=") + stored + '\n';
      file.write(text);
    }
    SettingsRegistry registry;
    REQUIRE(registerCommonSections(registry));
    Settings settings(registry, iniPath(dir));
    CAPTURE(stored);
    CHECK(settings.storedVersion(QStringLiteral("interface")) == 2);
    CHECK(settings.value(keys::Effects).toString() == QLatin1String(expected));
    CHECK(settings.value(keys::Scale).toDouble() == 1.25);
    QSettings raw(iniPath(dir), QSettings::IniFormat);
    CHECK_FALSE(raw.contains(QStringLiteral("interface/reduceMotion")));
  }
}
