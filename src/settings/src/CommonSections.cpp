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

  bool registerCommonSections(SettingsRegistry& registry) {
    bool ok = true;
    for (SectionDescriptor section : {accountSection(), interfaceSection(), diagnosticsSection(), windowSection()}) {
      ok = registry.registerSection(std::move(section)) && ok;
    }
    return ok;
  }

} // namespace jfd::settings
