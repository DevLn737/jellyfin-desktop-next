#include "../support/DoctestQt.h"

#include <jfd/settings/CommonSections.h>
#include <jfd/settings/LegacyImport.h>
#include <jfd/settings/Settings.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace jfd::settings;

namespace {

  // Like the player's section: one value that imports as it is, one that
  // needs a translation the section does not give.
  SectionDescriptor playbackLikeSection() {
    SectionDescriptor section;
    section.id = QStringLiteral("playback");
    section.context = "Test";
    section.title = "Playback";

    SettingDescriptor size;
    size.key = QStringLiteral("subtitleSize");
    size.type = SettingType::Choice;
    size.defaultValue = QStringLiteral("-1");
    size.label = "Subtitle size";
    for (const char* value : {"-1", "18", "24", "32", "42", "60"}) {
      size.choices.append({QString::fromLatin1(value), "size"});
    }
    size.legacyKey = QStringLiteral("subtitles.size");
    section.settings.append(size);

    SettingDescriptor hwdec;
    hwdec.key = QStringLiteral("hardwareDecoding");
    hwdec.type = SettingType::Choice;
    hwdec.defaultValue = QStringLiteral("auto-safe");
    hwdec.label = "Hardware decoding";
    hwdec.choices = {{QStringLiteral("auto-safe"), "On"}, {QStringLiteral("auto-copy-safe"), "Copy"},
                     {QStringLiteral("no"), "Off"}};
    hwdec.legacyKey = QStringLiteral("video.hardwareDecoding");
    section.settings.append(hwdec);
    return section;
  }

  QByteArray digest(const QString& directory) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& name : QDir(directory).entryList(QDir::Files, QDir::Name)) {
      QFile file(directory + QLatin1Char('/') + name);
      REQUIRE(file.open(QIODevice::ReadOnly));
      hash.addData(name.toUtf8());
      hash.addData(file.readAll());
    }
    return hash.result();
  }

  // A copy, so a write to the profile would show and not touch the sources.
  QString copyFixture(const QTemporaryDir& into) {
    const QString source = QStringLiteral(JFD_LEGACY_PROFILE_FIXTURE);
    const QString target = into.filePath(QStringLiteral("jellyfinmediaplayer"));
    REQUIRE(QDir().mkpath(target));
    for (const QString& name : QDir(source).entryList(QDir::Files)) {
      REQUIRE(QFile::copy(source + QLatin1Char('/') + name, target + QLatin1Char('/') + name));
    }
    return target;
  }

} // namespace

TEST_CASE("a 2.x profile is read: settings, window state and the server") {
  QTemporaryDir dir;
  const auto profile = readLegacyProfile(copyFixture(dir));
  REQUIRE(profile.has_value());
  CHECK(profile->serverAddress == QStringLiteral("https://jellyfin.example.org:8920"));
  CHECK(profile->values.value(QStringLiteral("subtitles.size")).toInt() == 42);
  CHECK(profile->values.value(QStringLiteral("video.hardwareDecoding")).toString() == QStringLiteral("enabled"));
  CHECK(profile->values.value(QStringLiteral("state.geometry")).toMap().value(QStringLiteral("width")).toInt() ==
        1600);
}

TEST_CASE("a folder without a 2.x profile is passed over") {
  QTemporaryDir empty;
  QTemporaryDir dir;
  const QString real = copyFixture(dir);
  CHECK_FALSE(readLegacyProfile(empty.path()).has_value());
  const auto found = findLegacyProfile({empty.path(), real});
  REQUIRE(found.has_value());
  CHECK(found->directory == real);
  CHECK_FALSE(findLegacyProfile({empty.path()}).has_value());
}

TEST_CASE("settings with a legacyKey take their 2.x values, once, and the profile stays as it was") {
  QTemporaryDir dir;
  const QString legacyDir = copyFixture(dir);
  const QByteArray before = digest(legacyDir);

  SettingsRegistry registry;
  REQUIRE(registerCommonSections(registry));
  REQUIRE(registry.registerSection(playbackLikeSection()));
  Settings settings(registry, dir.filePath(QStringLiteral("settings.ini")));

  const auto profile = readLegacyProfile(legacyDir);
  REQUIRE(profile.has_value());
  const LegacyImportResult result = importLegacySettings(settings, *profile);

  // Every registered legacyKey that the profile has.
  QStringList expected = {keys::LogLevel, keys::WindowGeometry, keys::WindowMaximized, keys::WindowFullScreen,
                          QStringLiteral("playback/subtitleSize")};
  expected.sort();
  QStringList imported = result.imported;
  imported.sort();
  CHECK(imported == expected);
  CHECK(result.skipped == QStringList{QStringLiteral("playback/hardwareDecoding")});

  CHECK(settings.value(keys::LogLevel).toString() == QStringLiteral("warning"));
  CHECK(settings.value(keys::WindowGeometry).toString() == QStringLiteral("160,90,1600,900"));
  CHECK(settings.value(keys::WindowMaximized).toBool() == false);
  CHECK(settings.value(keys::WindowFullScreen).toBool() == true);
  CHECK(settings.value(QStringLiteral("playback/subtitleSize")).toString() == QStringLiteral("42"));
  CHECK(settings.value(QStringLiteral("playback/hardwareDecoding")).toString() == QStringLiteral("auto-safe"));

  // Again: nothing changes.
  QSignalSpy changed(&settings, &Settings::valueChanged);
  importLegacySettings(settings, *profile);
  CHECK(changed.count() == 0);

  CHECK(digest(legacyDir) == before);
}

TEST_CASE("a translation turns the 2.x value into this client's") {
  QTemporaryDir dir;
  SectionDescriptor section = playbackLikeSection();
  section.settings[1].fromLegacy = [](const QVariant& legacy) -> QVariant {
    return legacy.toString() == QLatin1String("enabled") ? QVariant(QStringLiteral("auto-safe")) : QVariant();
  };
  SettingsRegistry registry;
  REQUIRE(registry.registerSection(section));
  Settings settings(registry, dir.filePath(QStringLiteral("settings.ini")));
  REQUIRE(settings.setValue(QStringLiteral("playback/hardwareDecoding"), QStringLiteral("no")));

  LegacyProfile profile;
  profile.values.insert(QStringLiteral("video.hardwareDecoding"), QStringLiteral("enabled"));
  const LegacyImportResult result = importLegacySettings(settings, profile);
  CHECK(result.imported == QStringList{QStringLiteral("playback/hardwareDecoding")});
  CHECK(settings.value(QStringLiteral("playback/hardwareDecoding")).toString() == QStringLiteral("auto-safe"));
}

TEST_CASE("the 2.x log level follows the user's choice, not the 2.x default") {
  QTemporaryDir dir;
  SettingsRegistry registry;
  REQUIRE(registerCommonSections(registry));
  Settings settings(registry, dir.filePath(QStringLiteral("settings.ini")));
  const QString before = settings.value(keys::LogLevel).toString();

  LegacyProfile profile;
  profile.values.insert(QStringLiteral("main.logLevel"), QStringLiteral("debug"));
  importLegacySettings(settings, profile);
  CHECK(settings.value(keys::LogLevel).toString() == before);

  profile.values.insert(QStringLiteral("main.logLevel"), QStringLiteral("trace"));
  importLegacySettings(settings, profile);
  CHECK(settings.value(keys::LogLevel).toString() == QStringLiteral("debug"));
}

TEST_CASE("only an http or https server address is taken") {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("p"));
  REQUIRE(QDir().mkpath(path));
  const auto write = [&](const QByteArray& address) {
    QFile file(path + QStringLiteral("/jellyfinmediaplayer.conf"));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{\"sections\": {\"main\": {\"userWebClient\": \"" + address + "\"}}, \"version\": 7}");
  };
  write("http://192.168.1.20:8096/");
  CHECK(readLegacyProfile(path)->serverAddress == QStringLiteral("http://192.168.1.20:8096"));
  write("");
  CHECK(readLegacyProfile(path)->serverAddress.isEmpty());
  write("file:///etc/passwd");
  CHECK(readLegacyProfile(path)->serverAddress.isEmpty());
}
