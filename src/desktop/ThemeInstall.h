#pragma once

#include <jfd/settings/CommonSections.h>
#include <jfd/settings/Settings.h>
#include <jfd/ui/ThemeFile.h>

#include <QDir>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QQmlEngine;
class QQuickWindow;

namespace jfd::ui {
  class CommandRegistry;
}

namespace jfd::desktop {

  struct AppServices;

  // The theme files in the themes folder (next to the settings file).
  class ThemeCatalog {
  public:
    explicit ThemeCatalog(QDir directory);

    const QDir& directory() const { return m_directory; }
    // Reads the folder again. `installedFonts` lets a theme's font family be
    // checked; empty accepts any.
    void rescan(const QStringList& installedFonts = {});
    const ui::ThemeDefinition* find(const QString& id) const;
    // "file:<id>" choices with the theme's name, for the Theme setting.
    QList<settings::Choice> choices() const;
    // What went wrong in files that could not be used, and warnings about
    // those that could ("name: message"), for the log.
    const QStringList& problems() const { return m_problems; }

    // Checks `json` as a theme file and copies it into the folder under a
    // safe name; returns the id, or an empty string with `error` set.
    QString add(const QByteArray& json, const QString& suggestedName, QString* error, QStringList* warnings);

  private:
    QDir m_directory;
    QStringList m_installedFonts;
    QHash<QString, ui::ThemeDefinition> m_themes;
    QStringList m_problems;
  };

  // Applies the Appearance settings to the Theme and Overlay singletons,
  // live: theme (dark, light, like the system, a file), accent, radius and
  // font from the theme file. Also the actions of the Appearance section and
  // the "Reset theme" command.
  class ThemeManager : public QObject {
    Q_OBJECT

  public:
    ThemeManager(AppServices& services, ThemeCatalog& catalog, QQmlEngine& engine, QQuickWindow& window);

    // Back to the dark theme with the blue accent (also --reset-theme).
    void resetTheme();
    // The palette shown now.
    QVariantMap currentPalette() const { return m_palette; }
    bool darkBase() const { return m_darkBase; }

  public Q_SLOTS:
    // From the window: files dropped on it (a QVariant list of urls).
    void importDropped(const QVariant& urls);

  private:
    void apply();
    void rescan();
    void exportCurrent();
    void importFile(const QString& path);
    void message(const QString& text, bool error);

    AppServices& m_services;
    ThemeCatalog& m_catalog;
    QObject* m_theme;
    QObject* m_overlay;
    QQuickWindow& m_window;
    QVariantMap m_palette;
    bool m_darkBase = true;
  };

  // Resets the theme settings to their defaults; for --reset-theme, before
  // anything is shown.
  void resetThemeSettings(settings::Settings& settings);

  // The Appearance section's file themes as settings choices.
  settings::ThemeChoices themeChoices(const ThemeCatalog& catalog);

  // After the window loads: shows the chosen theme and handles the
  // Appearance actions. Needs installShell. Returns the manager (owned by
  // the window).
  ThemeManager* installAppearance(AppServices& services, ThemeCatalog& catalog, QQmlEngine& engine,
                                  QQuickWindow& window);

} // namespace jfd::desktop
