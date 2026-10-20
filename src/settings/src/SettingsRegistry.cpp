#include "jfd/settings/SettingsRegistry.h"

#include <QLoggingCategory>
#include <QSet>

#include <algorithm>

namespace jfd::settings {

  namespace {
    Q_LOGGING_CATEGORY(lcRegistry, "jfd.settings")
  }

  std::optional<std::pair<QString, QString>> splitKey(const QString& fullKey) {
    const qsizetype slash = fullKey.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == fullKey.size() - 1) {
      return std::nullopt;
    }
    return std::make_pair(fullKey.left(slash), fullKey.mid(slash + 1));
  }

  bool SettingsRegistry::registerSection(SectionDescriptor section) {
    if (section.id.isEmpty() || section.id.contains(QLatin1Char('/')) ||
        section.id == QLatin1String("schema")) {
      qCWarning(lcRegistry) << "invalid section id" << section.id;
      return false;
    }
    if (this->section(section.id) != nullptr) {
      qCWarning(lcRegistry) << "section registered twice:" << section.id;
      return false;
    }
    if (section.context == nullptr || section.title == nullptr || section.version < 1) {
      qCWarning(lcRegistry) << "section" << section.id << "needs a context, a title and a version >= 1";
      return false;
    }
    QSet<QString> keys;
    for (const SettingDescriptor& setting : section.settings) {
      if (setting.key.isEmpty() || setting.key.contains(QLatin1Char('/')) ||
          keys.contains(setting.key) || setting.label == nullptr) {
        qCWarning(lcRegistry) << "invalid or duplicate setting" << section.id << setting.key;
        return false;
      }
      keys.insert(setting.key);
      if (setting.type == SettingType::Action) {
        continue;
      }
      if (setting.type == SettingType::Choice && setting.choices.isEmpty()) {
        qCWarning(lcRegistry) << "choice setting without choices:" << section.id << setting.key;
        return false;
      }
      const QVariant coerced = coerce(setting, setting.defaultValue);
      if (!coerced.isValid() || coerced != setting.defaultValue) {
        qCWarning(lcRegistry) << "default does not match the type of" << section.id << setting.key;
        return false;
      }
    }
    // Every version below the current one needs exactly one migration.
    for (int version = 1; version < section.version; ++version) {
      const auto count = std::count_if(section.migrations.begin(), section.migrations.end(),
                                       [&](const Migration& m) { return m.fromVersion == version; });
      if (count != 1) {
        qCWarning(lcRegistry) << "section" << section.id << "needs one migration from version" << version;
        return false;
      }
    }
    for (const Migration& migration : section.migrations) {
      if (migration.fromVersion < 1 || migration.fromVersion >= section.version || !migration.apply) {
        qCWarning(lcRegistry) << "section" << section.id << "has a migration from version"
                              << migration.fromVersion << "outside 1 to" << section.version - 1;
        return false;
      }
    }
    m_sections.append(std::move(section));
    std::stable_sort(m_sections.begin(), m_sections.end(),
                     [](const SectionDescriptor& a, const SectionDescriptor& b) {
                       return a.order != b.order ? a.order < b.order : a.id < b.id;
                     });
    return true;
  }

  QList<SectionDescriptor> SettingsRegistry::sections() const { return m_sections; }

  const SectionDescriptor* SettingsRegistry::section(const QString& id) const {
    for (const SectionDescriptor& section : m_sections) {
      if (section.id == id) {
        return &section;
      }
    }
    return nullptr;
  }

  const SettingDescriptor* SettingsRegistry::setting(const QString& fullKey) const {
    const auto parts = splitKey(fullKey);
    if (!parts) {
      return nullptr;
    }
    const SectionDescriptor* owner = section(parts->first);
    if (owner == nullptr) {
      return nullptr;
    }
    for (const SettingDescriptor& setting : owner->settings) {
      if (setting.key == parts->second) {
        return &setting;
      }
    }
    return nullptr;
  }

} // namespace jfd::settings
