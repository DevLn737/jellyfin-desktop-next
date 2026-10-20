#pragma once

#include "jfd/settings/Schema.h"

#include <QList>
#include <QString>

#include <optional>

namespace jfd::settings {

  // Every section of the application. Modules register their sections at
  // startup, before Settings opens the file; registering after that is a
  // programming error.
  class SettingsRegistry {
  public:
    // Fails (returns false and logs) on a duplicate section id, a duplicate
    // key, a default that does not match its type, or migrations that do not
    // lead from 1 to the section's version.
    bool registerSection(SectionDescriptor section);

    // Sorted by order, then id.
    QList<SectionDescriptor> sections() const;
    const SectionDescriptor* section(const QString& id) const;
    // "section/key"
    const SettingDescriptor* setting(const QString& fullKey) const;

  private:
    QList<SectionDescriptor> m_sections;
  };

  // Splits "section/key"; nullopt when there is no '/'.
  std::optional<std::pair<QString, QString>> splitKey(const QString& fullKey);

} // namespace jfd::settings
