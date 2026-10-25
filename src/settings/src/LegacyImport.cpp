#include "jfd/settings/LegacyImport.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QUrl>

namespace jfd::settings {

  namespace {
    Q_LOGGING_CATEGORY(lcLegacy, "jfd.settings.legacy")

    // {"sections": {"<section>": {"<key>": value}}, "version": N}, the format
    // of both files (SettingsComponent::saveSettings and saveStorage in 2.x).
    bool readSections(const QString& path, QVariantMap& values) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) {
        return false;
      }
      QJsonParseError error;
      const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
      if (!document.isObject()) {
        qCWarning(lcLegacy) << "cannot read" << path << error.errorString();
        return false;
      }
      const QJsonObject sections = document.object().value(QLatin1String("sections")).toObject();
      for (auto section = sections.begin(); section != sections.end(); ++section) {
        const QJsonObject keys = section.value().toObject();
        for (auto key = keys.begin(); key != keys.end(); ++key) {
          values.insert(section.key() + QLatin1Char('.') + key.key(), key.value().toVariant());
        }
      }
      return true;
    }

    QString serverAddress(const QVariant& value) {
      const QUrl url(value.toString().trimmed());
      if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) ||
          url.host().isEmpty()) {
        return {};
      }
      QString address = url.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
      while (address.endsWith(QLatin1Char('/'))) {
        address.chop(1);
      }
      return address;
    }
  } // namespace

  QStringList legacyProfileDirectories() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList directories;
#if defined(Q_OS_WIN)
    directories << data + QStringLiteral("/JellyfinMediaPlayer");
#else
    directories << data + QStringLiteral("/jellyfinmediaplayer");
#endif
    directories << data + QStringLiteral("/Jellyfin Media Player");
#if defined(Q_OS_LINUX)
    directories << QDir::homePath() +
                       QStringLiteral("/.var/app/com.github.iwalton3.jellyfin-media-player/data/jellyfinmediaplayer");
#endif
    return directories;
  }

  std::optional<LegacyProfile> readLegacyProfile(const QString& directory) {
    LegacyProfile profile;
    profile.directory = directory;
    if (!readSections(directory + QStringLiteral("/jellyfinmediaplayer.conf"), profile.values)) {
      return std::nullopt;
    }
    // Window state; missing in a profile that never closed a window.
    readSections(directory + QStringLiteral("/storage.json"), profile.values);
    profile.serverAddress = serverAddress(profile.values.value(QStringLiteral("main.userWebClient")));
    return profile;
  }

  std::optional<LegacyProfile> findLegacyProfile(const QStringList& directories) {
    for (const QString& directory : directories) {
      if (auto profile = readLegacyProfile(directory)) {
        return profile;
      }
    }
    return std::nullopt;
  }

  LegacyImportResult importLegacySettings(Settings& settings, const LegacyProfile& profile) {
    LegacyImportResult result;
    for (const SectionDescriptor& section : settings.registry().sections()) {
      for (const SettingDescriptor& setting : section.settings) {
        if (setting.legacyKey.isEmpty() || !profile.values.contains(setting.legacyKey)) {
          continue;
        }
        const QString key = section.id + QLatin1Char('/') + setting.key;
        const QVariant legacy = profile.values.value(setting.legacyKey);
        const QVariant value = setting.fromLegacy ? setting.fromLegacy(legacy) : legacy;
        if (value.isValid() && coerce(setting, value).isValid() && settings.setValue(key, value)) {
          result.imported.append(key);
        } else {
          qCInfo(lcLegacy) << "not imported:" << setting.legacyKey << legacy;
          result.skipped.append(key);
        }
      }
    }
    qCInfo(lcLegacy).noquote() << "imported" << result.imported.size() << "settings from" << profile.directory;
    return result;
  }

} // namespace jfd::settings
