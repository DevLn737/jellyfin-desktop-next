#include "jfd/settings/CommonSections.h"

#include <QCoreApplication>

namespace jfd::settings {

  namespace {
    constexpr const char* Context = "CommonSettings";

    QString key(QLatin1String fullKey) {
      return QString(fullKey).section(QLatin1Char('/'), 1);
    }

    SectionDescriptor accountSection() {
      SectionDescriptor section;
      section.id = QStringLiteral("account");
      section.context = Context;
      section.title = QT_TRANSLATE_NOOP("CommonSettings", "Account");
      section.order = 0;

      SettingDescriptor signOut;
      signOut.key = key(keys::SignOut);
      signOut.type = SettingType::Action;
      signOut.label = QT_TRANSLATE_NOOP("CommonSettings", "Sign out");
      section.settings.append(signOut);

      SettingDescriptor changeServer;
      changeServer.key = key(keys::ChangeServer);
      changeServer.type = SettingType::Action;
      changeServer.label = QT_TRANSLATE_NOOP("CommonSettings", "Change server");
      section.settings.append(changeServer);
      return section;
    }

    SectionDescriptor interfaceSection() {
      SectionDescriptor section;
      section.id = QStringLiteral("interface");
      section.context = Context;
      section.title = QT_TRANSLATE_NOOP("CommonSettings", "Interface");
      section.order = 10;

      SettingDescriptor language;
      language.key = key(keys::Language);
      language.type = SettingType::Choice;
      language.defaultValue = QStringLiteral("system");
      language.label = QT_TRANSLATE_NOOP("CommonSettings", "Language");
      language.choices = {
          {QStringLiteral("system"), QT_TRANSLATE_NOOP("CommonSettings", "System language")},
          // Language names are shown in their own language.
          {QStringLiteral("en"), QT_TRANSLATE_NOOP("CommonSettings", "English")},
          {QStringLiteral("ru"), QT_TRANSLATE_NOOP("CommonSettings", "Russian (Русский)")},
      };
      language.requiresRestart = true;
      section.settings.append(language);

      SettingDescriptor scale;
      scale.key = key(keys::Scale);
      scale.type = SettingType::Double;
      scale.defaultValue = 1.0;
      scale.label = QT_TRANSLATE_NOOP("CommonSettings", "Interface scale");
      scale.description =
          QT_TRANSLATE_NOOP("CommonSettings", "On top of the scale set in the operating system.");
      scale.minimum = 0.75;
      scale.maximum = 2.0;
      scale.step = 0.05;
      scale.requiresRestart = true;
      section.settings.append(scale);

      // Version 1 had a "Reduce animations" switch; on becomes "on", off
      // becomes automatic.
      section.version = 2;
      section.migrations = {
          {1,
           [](QVariantMap& values) {
             const QVariant reduced = values.take(QStringLiteral("reduceMotion"));
             if (reduced.isValid() && reduced.toBool()) {
               values.insert(QStringLiteral("effects"), QStringLiteral("on"));
             }
           }},
      };
      return section;
    }

    SectionDescriptor appearanceSection(ThemeChoices themeFiles) {
      SectionDescriptor section;
      section.id = QStringLiteral("appearance");
      section.context = Context;
      section.title = QT_TRANSLATE_NOOP("CommonSettings", "Appearance");
      section.order = 11;
      // The simplified-effects switch used to be in the interface section.
      section.adoptions = {{QString(QLatin1String("interface/effects")), key(keys::Effects)}};

      SettingDescriptor theme;
      theme.key = key(keys::Theme);
      theme.type = SettingType::Choice;
      theme.defaultValue = QStringLiteral("dark");
      theme.label = QT_TRANSLATE_NOOP("CommonSettings", "Theme");
      theme.description = QT_TRANSLATE_NOOP("CommonSettings", "Changes at once, no restart needed. The video player is always dark.");
      theme.dynamicChoices = [themeFiles = std::move(themeFiles)] {
        QList<Choice> choices = {
            {QStringLiteral("dark"), QT_TRANSLATE_NOOP("CommonSettings", "Dark")},
            {QStringLiteral("light"), QT_TRANSLATE_NOOP("CommonSettings", "Light")},
            {QStringLiteral("system"), QT_TRANSLATE_NOOP("CommonSettings", "Like the system")},
        };
        if (themeFiles) {
          choices.append(themeFiles());
        }
        return choices;
      };
      section.settings.append(theme);

      SettingDescriptor accent;
      accent.key = key(keys::Accent);
      accent.type = SettingType::Choice;
      accent.defaultValue = QStringLiteral("blue");
      accent.label = QT_TRANSLATE_NOOP("CommonSettings", "Accent colour");
      // The ids and names of the presets in jfd::ui::accentPresets().
      accent.choices = {
          {QStringLiteral("blue"), QT_TRANSLATE_NOOP("Appearance", "Blue")},
          {QStringLiteral("purple"), QT_TRANSLATE_NOOP("Appearance", "Purple")},
          {QStringLiteral("pink"), QT_TRANSLATE_NOOP("Appearance", "Pink")},
          {QStringLiteral("red"), QT_TRANSLATE_NOOP("Appearance", "Red")},
          {QStringLiteral("orange"), QT_TRANSLATE_NOOP("Appearance", "Orange")},
          {QStringLiteral("yellow"), QT_TRANSLATE_NOOP("Appearance", "Yellow")},
          {QStringLiteral("green"), QT_TRANSLATE_NOOP("Appearance", "Green")},
          {QStringLiteral("teal"), QT_TRANSLATE_NOOP("Appearance", "Teal")},
          {QStringLiteral("custom"), QT_TRANSLATE_NOOP("CommonSettings", "Custom")},
      };
      section.settings.append(accent);

      SettingDescriptor custom;
      custom.key = key(keys::AccentCustom);
      custom.type = SettingType::String;
      custom.defaultValue = QStringLiteral("#00a4dc");
      custom.label = QT_TRANSLATE_NOOP("CommonSettings", "Custom accent colour");
      custom.description = QT_TRANSLATE_NOOP("CommonSettings", "Like #00a4dc. Used when the accent colour is Custom.");
      section.settings.append(custom);

      SettingDescriptor effects;
      effects.key = key(keys::Effects);
      effects.type = SettingType::Choice;
      effects.defaultValue = QStringLiteral("auto");
      effects.label = QT_TRANSLATE_NOOP("CommonSettings", "Simplified effects");
      effects.description = QT_TRANSLATE_NOOP(
          "CommonSettings", "No blur and shorter animations, for slower graphics. Automatic: on with integrated graphics.");
      effects.choices = {
          {QStringLiteral("auto"), QT_TRANSLATE_NOOP("CommonSettings", "Automatic")},
          {QStringLiteral("on"), QT_TRANSLATE_NOOP("CommonSettings", "On")},
          {QStringLiteral("off"), QT_TRANSLATE_NOOP("CommonSettings", "Off")},
      };
      section.settings.append(effects);

      SettingDescriptor homeSections;
      homeSections.key = key(keys::HomeSections);
      homeSections.type = SettingType::String;
      homeSections.defaultValue = QStringLiteral("[]");
      homeSections.label = QT_TRANSLATE_NOOP("CommonSettings", "Home screen rows");
      homeSections.hidden = true;
      section.settings.append(homeSections);

      SettingDescriptor customizeHome;
      customizeHome.key = key(keys::CustomizeHome);
      customizeHome.type = SettingType::Action;
      customizeHome.label = QT_TRANSLATE_NOOP("CommonSettings", "Customize home");
      section.settings.append(customizeHome);

      for (const auto& [fullKey, text] :
           {std::pair{keys::OpenThemeFolder, QT_TRANSLATE_NOOP("CommonSettings", "Open the themes folder")},
            std::pair{keys::ReloadThemes, QT_TRANSLATE_NOOP("CommonSettings", "Reload themes")},
            std::pair{keys::ExportTheme, QT_TRANSLATE_NOOP("CommonSettings", "Save the current theme as a file")}}) {
        SettingDescriptor action;
        action.key = key(fullKey);
        action.type = SettingType::Action;
        action.label = text;
        section.settings.append(action);
      }
      return section;
    }

    SectionDescriptor diagnosticsSection() {
      SectionDescriptor section;
      section.id = QStringLiteral("diagnostics");
      section.context = Context;
      section.title = QT_TRANSLATE_NOOP("CommonSettings", "Diagnostics");
      section.order = 1000;

      SettingDescriptor logLevel;
      logLevel.key = key(keys::LogLevel);
      logLevel.type = SettingType::Choice;
      logLevel.defaultValue = QStringLiteral("info");
      logLevel.label = QT_TRANSLATE_NOOP("CommonSettings", "Log level");
      logLevel.choices = {
          {QStringLiteral("error"), QT_TRANSLATE_NOOP("CommonSettings", "Errors only")},
          {QStringLiteral("warning"), QT_TRANSLATE_NOOP("CommonSettings", "Warnings")},
          {QStringLiteral("info"), QT_TRANSLATE_NOOP("CommonSettings", "Information")},
          {QStringLiteral("debug"), QT_TRANSLATE_NOOP("CommonSettings", "Debug")},
      };
      logLevel.legacyKey = QStringLiteral("main.logLevel");
      logLevel.fromLegacy = [](const QVariant& legacy) -> QVariant {
        // "debug" is the 2.x default, and the setting was hidden there, so it
        // says nothing about the user: keep this client's default.
        const QString level = legacy.toString();
        if (level == QLatin1String("trace")) return QStringLiteral("debug");
        if (level == QLatin1String("info")) return QStringLiteral("info");
        if (level == QLatin1String("warn")) return QStringLiteral("warning");
        if (level == QLatin1String("error") || level == QLatin1String("fatal") || level == QLatin1String("disable"))
          return QStringLiteral("error");
        return {};
      };
      section.settings.append(logLevel);

      SettingDescriptor openLogs;
      openLogs.key = key(keys::OpenLogFolder);
      openLogs.type = SettingType::Action;
      openLogs.label = QT_TRANSLATE_NOOP("CommonSettings", "Open log folder");
      section.settings.append(openLogs);

      SettingDescriptor saveReport;
      saveReport.key = key(keys::SaveReport);
      saveReport.type = SettingType::Action;
      saveReport.label = QT_TRANSLATE_NOOP("CommonSettings", "Save diagnostic report");
      saveReport.description = QT_TRANSLATE_NOOP(
          "CommonSettings",
          "A zip with the logs, versions, graphics and session journal, without passwords, access keys or "
          "server addresses. Nothing is sent anywhere.");
      section.settings.append(saveReport);
      return section;
    }

    SectionDescriptor windowSection() {
      SectionDescriptor section;
      section.id = QStringLiteral("window");
      section.context = Context;
      section.title = QT_TRANSLATE_NOOP("CommonSettings", "Window");
      section.hidden = true;

      SettingDescriptor geometry;
      geometry.key = key(keys::WindowGeometry);
      geometry.type = SettingType::String;
      geometry.defaultValue = QString();
      geometry.label = "geometry";
      geometry.legacyKey = QStringLiteral("state.geometry");
      // 2.x: {"x", "y", "width", "height"}; here "x,y,width,height".
      geometry.fromLegacy = [](const QVariant& legacy) -> QVariant {
        const QVariantMap rect = legacy.toMap();
        int values[4] = {};
        const char* names[4] = {"x", "y", "width", "height"};
        for (int i = 0; i < 4; ++i) {
          bool ok = false;
          values[i] = rect.value(QLatin1String(names[i])).toInt(&ok);
          if (!ok) return {};
        }
        if (values[2] <= 0 || values[3] <= 0) return {};
        return QStringLiteral("%1,%2,%3,%4").arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]);
      };
      section.settings.append(geometry);

      SettingDescriptor screen;
      screen.key = key(keys::WindowScreen);
      screen.type = SettingType::String;
      screen.defaultValue = QString();
      screen.label = "screen";
      section.settings.append(screen);

      SettingDescriptor maximized;
      maximized.key = key(keys::WindowMaximized);
      maximized.type = SettingType::Bool;
      maximized.defaultValue = false;
      maximized.label = "maximized";
      maximized.legacyKey = QStringLiteral("state.maximized");
      section.settings.append(maximized);

      SettingDescriptor fullScreen;
      fullScreen.key = key(keys::WindowFullScreen);
      fullScreen.type = SettingType::Bool;
      fullScreen.defaultValue = false;
      fullScreen.label = "fullScreen";
      fullScreen.legacyKey = QStringLiteral("main.fullscreen");
      section.settings.append(fullScreen);
      return section;
    }
  } // namespace

  bool registerCommonSections(SettingsRegistry& registry, ThemeChoices themeFiles) {
    bool ok = true;
    for (SectionDescriptor section : {accountSection(), interfaceSection(), appearanceSection(std::move(themeFiles)),
                                      diagnosticsSection(), windowSection()}) {
      ok = registry.registerSection(std::move(section)) && ok;
    }
    return ok;
  }

} // namespace jfd::settings
