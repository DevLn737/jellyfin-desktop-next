#include "jfd/ui/SettingsModel.h"

#include <jfd/settings/Settings.h>

#include <QCoreApplication>
#include <QDir>
#include <QLoggingCategory>

namespace jfd::ui {

  using settings::SettingType;

  namespace {
    Q_LOGGING_CATEGORY(lcSettingsUi, "jfd.ui.settings")

    QString translated(const char* context, const char* text) {
      return text == nullptr ? QString() : QCoreApplication::translate(context, text);
    }
  } // namespace

  SettingsModel::SettingsModel(settings::Settings& settings, QObject* parent)
      : QAbstractListModel(parent), m_settings(settings) {
    const auto& registry = settings.registry();
    // Rows point into the registry's sections, which do not change once
    // the store is open.
    for (const settings::SectionDescriptor& listed : registry.sections()) {
      const settings::SectionDescriptor* section = registry.section(listed.id);
      if (section->hidden) {
        continue;
      }
      for (const settings::SettingDescriptor& setting : section->settings) {
        if (!setting.hidden) {
          m_rows.append({section, &setting, section->id + QLatin1Char('/') + setting.key});
        }
      }
    }
    connect(&settings, &settings::Settings::valueChanged, this,
            [this](const QString& key) { onValueChanged(key); });
    connect(&settings, &settings::Settings::choicesChanged, this, [this](const QString& key) {
      const int row = indexOf(key);
      if (row >= 0) {
        Q_EMIT dataChanged(index(row), index(row), {ChoicesRole});
      }
    });
  }

  int SettingsModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
  }

  QString SettingsModel::typeName(int type) {
    switch (SettingType(type)) {
    case SettingType::Bool:
      return QStringLiteral("bool");
    case SettingType::Int:
      return QStringLiteral("int");
    case SettingType::Double:
      return QStringLiteral("double");
    case SettingType::String:
      return QStringLiteral("string");
    case SettingType::Choice:
      return QStringLiteral("choice");
    case SettingType::Action:
      return QStringLiteral("action");
    case SettingType::Folder:
      return QStringLiteral("folder");
    }
    return {};
  }

  bool SettingsModel::matches(int row, const QString& text) const {
    if (row < 0 || row >= m_rows.size()) return false;
    const QStringList words = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty()) return true;
    const Row& r = m_rows[row];
    const char* context = r.section->context;
    const settings::SettingDescriptor& setting = *r.setting;
    QStringList haystack;
    const auto add = [&](const char* source) {
      if (source == nullptr) return;
      haystack.append(QString::fromUtf8(source));
      haystack.append(translated(context, source));
    };
    add(r.section->title);
    add(setting.label);
    add(setting.description);
    for (const settings::Choice& choice : settings::choicesOf(setting)) {
      if (choice.label) add(choice.label);
      else haystack.append(choice.text);
    }
    const QString all = haystack.join(QLatin1Char('\n'));
    for (const QString& word : words)
      if (!all.contains(word, Qt::CaseInsensitive)) return false;
    return true;
  }

  SettingsSearch::SettingsSearch(QObject* parent) : QSortFilterProxyModel(parent) {
    connect(this, &QAbstractItemModel::rowsInserted, this, &SettingsSearch::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &SettingsSearch::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &SettingsSearch::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &SettingsSearch::countChanged);
  }

  void SettingsSearch::setSettings(SettingsModel* settings) {
    if (m_settings == settings) return;
    m_settings = settings;
    setSourceModel(settings);
    Q_EMIT settingsChanged();
    Q_EMIT countChanged();
  }

  void SettingsSearch::setText(const QString& text) {
    if (m_text == text) return;
    beginFilterChange();
    m_text = text;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    Q_EMIT textChanged();
    Q_EMIT countChanged();
  }

  QVariantList SettingsSearch::sections() const {
    QVariantList out;
    QString last;
    for (int row = 0; row < rowCount(); ++row) {
      const QModelIndex at = index(row, 0);
      const QString id = at.data(SettingsModel::SectionIdRole).toString();
      if (row > 0 && id == last) continue;
      last = id;
      out.append(QVariantMap{{QStringLiteral("id"), id},
                             {QStringLiteral("title"), at.data(SettingsModel::SectionTitleRole)},
                             {QStringLiteral("row"), row}});
    }
    return out;
  }

  bool SettingsSearch::filterAcceptsRow(int sourceRow, const QModelIndex&) const {
    return m_settings == nullptr || m_settings->matches(sourceRow, m_text);
  }

  QVariant SettingsModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
      return {};
    }
    const Row& row = m_rows[index.row()];
    const char* context = row.section->context;
    const settings::SettingDescriptor& setting = *row.setting;
    switch (role) {
    case KeyRole:
      return row.key;
    case SectionIdRole:
      return row.section->id;
    case SectionTitleRole:
      return translated(context, row.section->title);
    case TypeRole:
      return typeName(int(setting.type));
    case LabelRole:
    case Qt::DisplayRole:
      return translated(context, setting.label);
    case DescriptionRole:
      return translated(context, setting.description);
    case ValueRole:
      return m_settings.value(row.key);
    case ChoicesRole: {
      QVariantList choices;
      for (const settings::Choice& choice : settings::choicesOf(setting)) {
        choices.append(QVariantMap{{QStringLiteral("value"), choice.value},
                                   {QStringLiteral("label"),
                                    choice.label ? translated(context, choice.label) : choice.text}});
      }
      return choices;
    }
    case MinimumRole:
      return setting.minimum;
    case MaximumRole:
      return setting.maximum;
    case StepRole:
      return setting.step;
    case UnitRole:
      return translated(context, setting.unit);
    case RequiresRestartRole:
      return setting.requiresRestart;
    case IsDefaultRole:
      return m_settings.isDefault(row.key);
    case ConfirmationRole:
      return translated(context, setting.confirmation);
    default:
      return {};
    }
  }

  QHash<int, QByteArray> SettingsModel::roleNames() const {
    return {
        {KeyRole, "key"},
        {SectionIdRole, "sectionId"},
        {SectionTitleRole, "sectionTitle"},
        {TypeRole, "type"},
        {LabelRole, "label"},
        {DescriptionRole, "description"},
        {ValueRole, "value"},
        {ChoicesRole, "choices"},
        {MinimumRole, "minimum"},
        {MaximumRole, "maximum"},
        {StepRole, "step"},
        {UnitRole, "unit"},
        {RequiresRestartRole, "requiresRestart"},
        {IsDefaultRole, "isDefault"},
        {ConfirmationRole, "confirmation"},
    };
  }

  int SettingsModel::indexOf(const QString& key) const {
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
      if (m_rows[i].key == key) {
        return int(i);
      }
    }
    return -1;
  }

  void SettingsModel::onValueChanged(const QString& key) {
    const int row = indexOf(key);
    if (row >= 0) {
      const QModelIndex changed = index(row);
      Q_EMIT dataChanged(changed, changed, {ValueRole, IsDefaultRole});
    }
  }

  AppSettings::AppSettings(QObject* parent) : QObject(parent) {}

  void AppSettings::attach(settings::Settings* settings) {
    if (m_settings != nullptr) {
      qCWarning(lcSettingsUi) << "settings attached twice";
      return;
    }
    m_settings = settings;
    m_model = new SettingsModel(*settings, this);
    connect(settings, &settings::Settings::valueChanged, this,
            [this](const QString& key, const QVariant& value) {
              ++m_revision;
              Q_EMIT revisionChanged();
              Q_EMIT valueChanged(key, value);
            });
    Q_EMIT attached();
  }

  QVariant AppSettings::value(const QString& key) const {
    return m_settings != nullptr ? m_settings->value(key) : QVariant();
  }

  bool AppSettings::setValue(const QString& key, const QVariant& value) {
    return m_settings != nullptr && m_settings->setValue(key, value);
  }

  void AppSettings::reset(const QString& key) {
    if (m_settings != nullptr) {
      m_settings->reset(key);
    }
  }

  void AppSettings::trigger(const QString& key) {
    if (m_settings != nullptr) {
      m_settings->trigger(key);
    }
  }

  QString AppSettings::localPath(const QUrl& url) const {
    return url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : QString();
  }

  QUrl AppSettings::folderUrl(const QString& path) const {
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(QDir::fromNativeSeparators(path));
  }

} // namespace jfd::ui
