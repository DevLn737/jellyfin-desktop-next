#pragma once

#include <QAbstractListModel>
#include <QList>
#include <qqmlintegration.h>

namespace jfd::settings {
  class Settings;
  struct SectionDescriptor;
  struct SettingDescriptor;
} // namespace jfd::settings

namespace jfd::ui {

  // Every visible setting of every visible section, in screen order, for
  // the settings screen. Labels come translated. Values follow the store.
  class SettingsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use AppSettings.model")
  public:
    enum Role {
      KeyRole = Qt::UserRole + 1,
      SectionIdRole,
      SectionTitleRole,
      TypeRole,
      LabelRole,
      DescriptionRole,
      ValueRole,
      ChoicesRole,
      MinimumRole,
      MaximumRole,
      StepRole,
      UnitRole,
      RequiresRestartRole,
      IsDefaultRole,
    };

    explicit SettingsModel(settings::Settings& settings, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The row of a full key, or -1.
    Q_INVOKABLE int indexOf(const QString& key) const;

    // Type names used in the "type" role.
    static QString typeName(int type);

  private:
    struct Row {
      const settings::SectionDescriptor* section;
      const settings::SettingDescriptor* setting;
      QString key;
    };

    void onValueChanged(const QString& key);

    settings::Settings& m_settings;
    QList<Row> m_rows;
  };

  // The QML face of the settings store: AppSettings.value("interface/scale"),
  // AppSettings.setValue(...), AppSettings.trigger(...). Bindings that read
  // values should also read `revision`, which changes with every value, so
  // they re-evaluate:
  //   enabled: AppSettings.revision, AppSettings.value("discord/enabled")
  class AppSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(jfd::ui::SettingsModel* model READ model NOTIFY attached)
    Q_PROPERTY(bool ready READ ready NOTIFY attached)

  public:
    explicit AppSettings(QObject* parent = nullptr);

    // Called once by the application before loading the window.
    void attach(settings::Settings* settings);
    settings::Settings* settings() const { return m_settings; }

    int revision() const { return m_revision; }
    SettingsModel* model() const { return m_model; }
    bool ready() const { return m_settings != nullptr; }

    Q_INVOKABLE QVariant value(const QString& key) const;
    Q_INVOKABLE bool setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void reset(const QString& key);
    Q_INVOKABLE void trigger(const QString& key);

  Q_SIGNALS:
    void revisionChanged();
    void valueChanged(const QString& key, const QVariant& value);
    void attached();

  private:
    settings::Settings* m_settings = nullptr;
    SettingsModel* m_model = nullptr;
    int m_revision = 0;
  };

} // namespace jfd::ui
