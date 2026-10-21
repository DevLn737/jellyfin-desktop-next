#pragma once

#include "jfd/settings/SettingsRegistry.h"

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <memory>

namespace jfd::settings {

  // The stored values of every registered setting, in an INI file in the
  // user's profile. Reads return the default for anything not stored; writes
  // are validated against the schema and saved at once.
  //
  // Opening the file migrates each section from its stored version to the
  // registered one. Values of unknown sections and keys are kept untouched,
  // so an older build does not lose what a newer one wrote.
  class Settings : public QObject {
    Q_OBJECT
  public:
    Settings(const SettingsRegistry& registry, const QString& filePath, QObject* parent = nullptr);
    ~Settings() override;

    // The default file: settings.ini in the per-user configuration folder.
    static QString defaultFilePath();

    const SettingsRegistry& registry() const { return m_registry; }
    QString filePath() const;

    // "section/key". An unregistered key returns an invalid QVariant.
    Q_INVOKABLE QVariant value(const QString& key) const;
    // False when the key is not registered or the value is invalid for it.
    // Emits valueChanged only when the stored value actually changes.
    Q_INVOKABLE bool setValue(const QString& key, const QVariant& value);
    // Back to the default; the stored entry is removed.
    Q_INVOKABLE void reset(const QString& key);
    Q_INVOKABLE bool isDefault(const QString& key) const;

    // For Action settings; emits actionTriggered.
    Q_INVOKABLE void trigger(const QString& key);

    // The stored schema version of a section (0 when never written).
    int storedVersion(const QString& sectionId) const;

  Q_SIGNALS:
    void valueChanged(const QString& key, const QVariant& value);
    void actionTriggered(const QString& key);

  private:
    void migrate();

    const SettingsRegistry& m_registry;
    std::unique_ptr<QSettings> m_store;
  };

} // namespace jfd::settings
