#include "jfd/settings/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace jfd::settings {

  namespace {
    Q_LOGGING_CATEGORY(lcSettings, "jfd.settings")

    QString versionKey(const QString& sectionId) {
      return QStringLiteral("schema/") + sectionId;
    }
  } // namespace

  Settings::Settings(const SettingsRegistry& registry, const QString& filePath, QObject* parent)
      : QObject(parent), m_registry(registry),
        m_store(std::make_unique<QSettings>(filePath, QSettings::IniFormat)) {
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    migrate();
    m_store->sync();
    if (m_store->status() != QSettings::NoError) {
      qCWarning(lcSettings) << "cannot use" << filePath << "- settings will not be saved";
    }
  }

  Settings::~Settings() = default;

  QString Settings::defaultFilePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           QStringLiteral("/settings.ini");
  }

  QString Settings::filePath() const { return m_store->fileName(); }

  QVariant Settings::value(const QString& key) const {
    const SettingDescriptor* setting = m_registry.setting(key);
    if (setting == nullptr || setting->type == SettingType::Action) {
      return {};
    }
    const QVariant stored = coerce(*setting, m_store->value(key));
    return stored.isValid() ? stored : coerce(*setting, setting->defaultValue);
  }

  bool Settings::setValue(const QString& key, const QVariant& value) {
    const SettingDescriptor* setting = m_registry.setting(key);
    if (setting == nullptr) {
      qCWarning(lcSettings) << "unknown setting" << key;
      return false;
    }
    const QVariant coerced = coerce(*setting, value);
    if (!coerced.isValid()) {
      qCWarning(lcSettings) << "invalid value for" << key << value;
      return false;
    }
    const QVariant previous = this->value(key);
    m_store->setValue(key, coerced);
    m_store->sync();
    if (previous != coerced) {
      Q_EMIT valueChanged(key, coerced);
    }
    return true;
  }

  void Settings::reset(const QString& key) {
    const SettingDescriptor* setting = m_registry.setting(key);
    if (setting == nullptr || setting->type == SettingType::Action) {
      return;
    }
    const QVariant previous = value(key);
    m_store->remove(key);
    m_store->sync();
    const QVariant current = value(key);
    if (previous != current) {
      Q_EMIT valueChanged(key, current);
    }
  }

  bool Settings::isDefault(const QString& key) const {
    const SettingDescriptor* setting = m_registry.setting(key);
    if (setting == nullptr || setting->type == SettingType::Action) {
      return true;
    }
    return value(key) == coerce(*setting, setting->defaultValue);
  }

  void Settings::trigger(const QString& key) {
    const SettingDescriptor* setting = m_registry.setting(key);
    if (setting == nullptr || setting->type != SettingType::Action) {
      qCWarning(lcSettings) << "not an action:" << key;
      return;
    }
    Q_EMIT actionTriggered(key);
  }

  int Settings::storedVersion(const QString& sectionId) const {
    return m_store->value(versionKey(sectionId), 0).toInt();
  }

  void Settings::migrate() {
    for (const SectionDescriptor& section : m_registry.sections()) {
      int version = storedVersion(section.id);
      m_store->beginGroup(section.id);
      const QStringList keys = m_store->childKeys();
      QVariantMap values;
      for (const QString& key : keys) {
        values.insert(key, m_store->value(key));
      }
      m_store->endGroup();

      if (version == 0) {
        // Never versioned: values without a version predate versioning and
        // are version 1; a section never written starts at the current one.
        version = values.isEmpty() ? section.version : 1;
      }
      if (version > section.version) {
        // Written by a newer build. Keep everything; reads fall back to
        // defaults where a value no longer fits.
        qCInfo(lcSettings) << "section" << section.id << "is version" << version << "but this build knows"
                           << section.version;
        continue;
      }
      if (version < section.version) {
        for (int from = version; from < section.version; ++from) {
          for (const Migration& migration : section.migrations) {
            if (migration.fromVersion == from) {
              migration.apply(values);
            }
          }
        }
        m_store->remove(section.id);
        m_store->beginGroup(section.id);
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
          m_store->setValue(it.key(), it.value());
        }
        m_store->endGroup();
        qCInfo(lcSettings) << "migrated section" << section.id << "from version" << version << "to"
                           << section.version;
      }
      m_store->setValue(versionKey(section.id), section.version);
    }
    // A value that changed sections (after the old section's own migrations,
    // which may have made it).
    for (const SectionDescriptor& section : m_registry.sections()) {
      for (const Adoption& adoption : section.adoptions) {
        const QString target = section.id + QLatin1Char('/') + adoption.key;
        if (m_store->contains(adoption.fromKey)) {
          if (!m_store->contains(target)) {
            m_store->setValue(target, m_store->value(adoption.fromKey));
          }
          m_store->remove(adoption.fromKey);
        }
      }
    }
  }

} // namespace jfd::settings
