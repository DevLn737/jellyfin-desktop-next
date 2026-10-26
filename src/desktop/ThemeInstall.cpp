#include "ThemeInstall.h"

#include "AppServices.h"

#include <jfd/ui/CommandRegistry.h>
#include <jfd/ui/ThemePalette.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStyleHints>
#include <QUrl>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcTheme, "jfd.app.theme")

    constexpr QLatin1String FilePrefix{"file:"};
    constexpr int DefaultRadius = 6;

    QString themeId(const QString& fileName) {
      return QFileInfo(fileName).completeBaseName();
    }
  } // namespace

  ThemeCatalog::ThemeCatalog(QDir directory) : m_directory(std::move(directory)) {
    rescan();
  }

  void ThemeCatalog::rescan(const QStringList& installedFonts) {
    if (!installedFonts.isEmpty()) {
      m_installedFonts = installedFonts;
    }
    m_themes.clear();
    m_problems.clear();
    const QFileInfoList files = m_directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
      QFile file(info.absoluteFilePath());
      if (info.size() > ui::MaxThemeFileBytes || !file.open(QIODevice::ReadOnly)) {
        m_problems.append(info.fileName() + QStringLiteral(": ") + QCoreApplication::translate("Appearance", "The file cannot be read or is too large."));
        continue;
      }
      const ui::ThemeLoadResult result =
          ui::parseTheme(file.read(ui::MaxThemeFileBytes + 1), themeId(info.fileName()), m_installedFonts);
      if (!result.ok) {
        m_problems.append(info.fileName() + QStringLiteral(": ") + result.error);
        continue;
      }
      for (const QString& warning : result.warnings) {
        m_problems.append(info.fileName() + QStringLiteral(": ") + warning);
      }
      ui::ThemeDefinition theme = result.theme;
      theme.id = themeId(info.fileName());
      m_themes.insert(theme.id, theme);
    }
    for (const QString& problem : std::as_const(m_problems)) {
      qCWarning(lcTheme).noquote() << problem;
    }
  }

  const ui::ThemeDefinition* ThemeCatalog::find(const QString& id) const {
    const auto it = m_themes.constFind(id);
    return it == m_themes.constEnd() ? nullptr : &*it;
  }

  QList<settings::Choice> ThemeCatalog::choices() const {
    QList<const ui::ThemeDefinition*> sorted;
    for (const ui::ThemeDefinition& theme : m_themes) {
      sorted.append(&theme);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
      return QString::localeAwareCompare(a->name, b->name) < 0;
    });
    QList<settings::Choice> choices;
    for (const ui::ThemeDefinition* theme : sorted) {
      choices.append(settings::Choice(FilePrefix + theme->id, nullptr, theme->name));
    }
    return choices;
  }

  QString ThemeCatalog::add(const QByteArray& json, const QString& suggestedName, QString* error,
                            QStringList* warnings) {
    if (json.size() > ui::MaxThemeFileBytes) {
      *error = QCoreApplication::translate("Appearance", "The theme file is too large (the limit is 64 KiB).");
      return {};
    }
    QString base = themeId(suggestedName).toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
    base = base.left(48);
    if (base.isEmpty() || base.startsWith(QLatin1Char('.'))) {
      base = QStringLiteral("theme");
    }
    const ui::ThemeLoadResult result = ui::parseTheme(json, base, m_installedFonts);
    if (!result.ok) {
      *error = result.error;
      return {};
    }
    if (!m_directory.mkpath(QStringLiteral("."))) {
      *error = QCoreApplication::translate("Appearance", "The themes folder cannot be created.");
      return {};
    }
    QString id = base;
    for (int n = 2; m_directory.exists(id + QStringLiteral(".json")); ++n) {
      id = base + QLatin1Char('-') + QString::number(n);
    }
    QFile file(m_directory.filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) {
      *error = QCoreApplication::translate("Appearance", "The theme file cannot be saved.");
      return {};
    }
    file.close();
    if (warnings != nullptr) {
      *warnings = result.warnings;
    }
    return id;
  }

  settings::ThemeChoices themeChoices(const ThemeCatalog& catalog) {
    return [&catalog] { return catalog.choices(); };
  }

  void resetThemeSettings(settings::Settings& settings) {
    settings.reset(settings::keys::Theme);
    settings.reset(settings::keys::Accent);
    settings.reset(settings::keys::AccentCustom);
  }

  ThemeManager::ThemeManager(AppServices& services, ThemeCatalog& catalog, QQmlEngine& engine, QQuickWindow& window)
      : QObject(&window),
        m_services(services),
        m_catalog(catalog),
        m_theme(engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme")),
        m_overlay(engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Overlay")),
        m_window(window) {
    Q_ASSERT(m_theme != nullptr && m_overlay != nullptr);
    settings::Settings& settings = *services.settings;
    connect(&settings, &settings::Settings::valueChanged, this, [this](const QString& key) {
      if (key == settings::keys::Theme || key == settings::keys::Accent || key == settings::keys::AccentCustom) {
        apply();
      }
    });
    connect(&settings, &settings::Settings::actionTriggered, this, [this](const QString& key) {
      if (key == settings::keys::OpenThemeFolder) {
        m_catalog.directory().mkpath(QStringLiteral("."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_catalog.directory().absolutePath()));
      } else if (key == settings::keys::ReloadThemes) {
        rescan();
      } else if (key == settings::keys::ExportTheme) {
        exportCurrent();
      }
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { apply(); });
    if (auto* commands = engine.singletonInstance<ui::CommandRegistry*>("Jfd.Ui.Shell", "CommandRegistry")) {
      ui::CommandRegistry::Command reset;
      reset.id = QStringLiteral("appearance.resetTheme");
      reset.title = QCoreApplication::translate("Appearance", "Reset theme");
      reset.run = [this] {
        resetTheme();
        message(QCoreApplication::translate("Appearance", "The theme is back to the default."), false);
      };
      commands->add(std::move(reset));
    }
    // The window forwards files dropped on it.
    if (window.metaObject()->indexOfSignal("themeFilesDropped(QVariant)") >= 0) {
      connect(&window, SIGNAL(themeFilesDropped(QVariant)), this, SLOT(importDropped(QVariant)));
    }
    apply();
  }

  void ThemeManager::resetTheme() {
    resetThemeSettings(*m_services.settings);
    apply();
  }

  void ThemeManager::message(const QString& text, bool error) {
    if (QObject* shell = m_window.findChild<QObject*>(QStringLiteral("shell"))) {
      QMetaObject::invokeMethod(shell, error ? "showError" : "showMessage", Q_ARG(QVariant, QVariant(text)));
    }
  }

  void ThemeManager::apply() {
    settings::Settings& settings = *m_services.settings;
    QString choice = settings.value(settings::keys::Theme).toString();
    const ui::ThemeDefinition* file = nullptr;
    bool dark = true;
    if (choice.startsWith(FilePrefix)) {
      file = m_catalog.find(choice.mid(FilePrefix.size()));
      if (file != nullptr) {
        dark = file->darkBase;
      } else {
        qCWarning(lcTheme) << "theme file" << choice << "is missing; using the dark theme";
      }
    } else if (choice == QLatin1String("light")) {
      dark = false;
    } else if (choice == QLatin1String("system")) {
      // Unknown (a platform without a setting) is dark.
      dark = QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    }

    // The accent from the settings; a theme file with an accent of its own
    // keeps it.
    QColor accent = ui::accentPresets().first().color;
    const QString accentId = settings.value(settings::keys::Accent).toString();
    if (accentId == QLatin1String("custom")) {
      const QColor custom = ui::parseAccent(settings.value(settings::keys::AccentCustom).toString());
      if (custom.isValid()) {
        accent = custom;
      }
    } else {
      for (const ui::AccentPreset& preset : ui::accentPresets()) {
        if (preset.id == accentId) {
          accent = preset.color;
        }
      }
    }

    QVariantMap palette;
    int radius = DefaultRadius;
    QString font;
    if (file != nullptr) {
      palette = file->ownAccent ? file->colors : ui::withAccent(file->colors, accent, dark);
      if (file->radius >= 0) {
        radius = file->radius;
      }
      font = file->fontFamily;
    } else {
      palette = ui::withAccent(dark ? ui::darkPalette() : ui::lightPalette(), accent, dark);
    }
    m_palette = palette;
    m_darkBase = dark;

    const QColor shownAccent(palette.value(QStringLiteral("accent")).toString());
    m_theme->setProperty("colors", palette);
    m_theme->setProperty("radiusOverride", radius);
    m_theme->setProperty("fontFamilyOverride", font);
    // What is drawn over the video stays dark, with the same accent.
    m_overlay->setProperty("colors", ui::withAccent(ui::darkPalette(), shownAccent, true));
  }

  void ThemeManager::rescan() {
    m_catalog.rescan(QFontDatabase::families());
    m_services.settings->notifyChoicesChanged(settings::keys::Theme);
    apply();
    const QStringList& problems = m_catalog.problems();
    if (problems.isEmpty()) {
      message(QCoreApplication::translate("Appearance", "Themes reloaded."), false);
    } else {
      message(QCoreApplication::translate("Appearance", "Themes reloaded. Problem: %1").arg(problems.first()), true);
    }
  }

  void ThemeManager::exportCurrent() {
    ui::ThemeDefinition theme;
    theme.name = QCoreApplication::translate("Appearance", "My theme");
    theme.darkBase = m_darkBase;
    theme.colors = m_palette;
    theme.ownAccent = true;
    QString error;
    QStringList warnings;
    const QString id = m_catalog.add(ui::exportTheme(theme), QStringLiteral("my-theme"), &error, &warnings);
    if (id.isEmpty()) {
      message(error, true);
      return;
    }
    m_catalog.rescan();
    m_services.settings->notifyChoicesChanged(settings::keys::Theme);
    message(QCoreApplication::translate("Appearance", "Saved as %1.json in the themes folder.").arg(id), false);
  }

  void ThemeManager::importFile(const QString& path) {
    QFile file(path);
    if (QFileInfo(path).size() > ui::MaxThemeFileBytes || !file.open(QIODevice::ReadOnly)) {
      message(QCoreApplication::translate("Appearance", "The theme file cannot be read or is too large."), true);
      return;
    }
    QString error;
    QStringList warnings;
    const QString id = m_catalog.add(file.read(ui::MaxThemeFileBytes + 1), QFileInfo(path).fileName(), &error, &warnings);
    if (id.isEmpty()) {
      message(error, true);
      return;
    }
    m_catalog.rescan();
    m_services.settings->notifyChoicesChanged(settings::keys::Theme);
    m_services.settings->setValue(settings::keys::Theme, FilePrefix + id);
    const ui::ThemeDefinition* theme = m_catalog.find(id);
    QString text = QCoreApplication::translate("Appearance", "Theme “%1” added.").arg(theme != nullptr ? theme->name : id);
    if (!warnings.isEmpty()) {
      text += QLatin1Char(' ') + warnings.first();
    }
    message(text, false);
  }

  void ThemeManager::importDropped(const QVariant& urls) {
    for (const QUrl& url : urls.value<QList<QUrl>>()) {
      if (url.isLocalFile() && url.fileName().endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
        importFile(url.toLocalFile());
      }
    }
  }

  ThemeManager* installAppearance(AppServices& services, ThemeCatalog& catalog, QQmlEngine& engine,
                                  QQuickWindow& window) {
    catalog.rescan(QFontDatabase::families());
    services.settings->notifyChoicesChanged(settings::keys::Theme);
    return new ThemeManager(services, catalog, engine, window);
  }

} // namespace jfd::desktop
