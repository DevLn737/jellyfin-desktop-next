#include "LegacyInstall.h"

#include <jfd/library/SignIn.h>
#include <jfd/ui/Navigator.h>

#include <QCoreApplication>
#include <QLoggingCategory>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcLegacy, "jfd.app.legacy")

    constexpr QLatin1String ImportAgain{"legacy/importAgain"};
    // The 2.x folder the first start imported from.
    constexpr QLatin1String ImportedFrom{"legacy/importedFrom"};

    QString importedText(int count) {
      return QCoreApplication::translate("LegacySettings", "Imported %n setting(s) from version 2.x.", nullptr,
                                         count);
    }

    void showMessage(QQuickWindow& window, const QString& text) {
      QObject* shell = window.findChild<QObject*>(QStringLiteral("shell"));
      if (shell != nullptr) {
        QMetaObject::invokeMethod(shell, "showMessage", Q_ARG(QVariant, QVariant(text)));
      }
    }

    // Only while no server is chosen: a restored session or a server the
    // user picked here comes first.
    void openServer(AppServices& services, const QString& address) {
      if (address.isEmpty() || services.signIn == nullptr ||
          services.signIn->step() != library::SignIn::Step::ChooseServer) {
        return;
      }
      qCInfo(lcLegacy) << "opening the 2.x server";
      services.signIn->connectToServer(address);
    }
  } // namespace

  LegacyImport findLegacyImport(bool smoke) {
    LegacyImport legacy;
    const QString override = qEnvironmentVariable("JFD_LEGACY_PROFILE");
    if (!override.isEmpty()) {
      legacy.profile = settings::readLegacyProfile(override);
    } else if (!smoke) {
      legacy.profile = settings::findLegacyProfile();
    }
    return legacy;
  }

  bool registerLegacySettings(settings::SettingsRegistry& registry, const LegacyImport& legacy) {
    if (!legacy.profile) {
      return true;
    }
    settings::SectionDescriptor section;
    section.id = QStringLiteral("legacy");
    section.context = "LegacySettings";
    section.title = QT_TRANSLATE_NOOP("LegacySettings", "Version 2.x");
    section.order = 900;

    settings::SettingDescriptor again;
    again.key = QString(ImportAgain).section(QLatin1Char('/'), 1);
    again.type = settings::SettingType::Action;
    again.label = QT_TRANSLATE_NOOP("LegacySettings", "Import settings from version 2.x");
    again.description = QT_TRANSLATE_NOOP(
        "LegacySettings",
        "The server address and the settings of the 2.x client on this computer. Settings changed here are "
        "replaced.");
    section.settings.append(again);

    settings::SettingDescriptor from;
    from.key = QString(ImportedFrom).section(QLatin1Char('/'), 1);
    from.type = settings::SettingType::String;
    from.defaultValue = QString();
    from.label = "importedFrom";
    from.hidden = true;
    section.settings.append(from);
    return registry.registerSection(std::move(section));
  }

  void importLegacyAtFirstStart(AppServices& services, LegacyImport& legacy, bool firstStart) {
    if (!legacy.profile || !firstStart) {
      return;
    }
    legacy.imported = int(settings::importLegacySettings(*services.settings, *legacy.profile).imported.size());
    services.settings->setValue(ImportedFrom, legacy.profile->directory);
  }

  void installLegacy(AppServices& services, LegacyImport& legacy, bool connectServer) {
    if (!legacy.profile) {
      return;
    }
    if (legacy.imported >= 0 && connectServer) {
      openServer(services, legacy.profile->serverAddress);
    }
    QObject::connect(services.settings.get(), &settings::Settings::actionTriggered, services.navigator,
                     [&services, &legacy, connectServer](const QString& key) {
                       if (key != ImportAgain) {
                         return;
                       }
                       // Read again: the 2.x client may have run since.
                       if (auto profile = settings::readLegacyProfile(legacy.profile->directory)) {
                         legacy.profile = std::move(profile);
                       }
                       const int count =
                           int(settings::importLegacySettings(*services.settings, *legacy.profile).imported.size());
                       if (legacy.window) {
                         showMessage(*legacy.window, importedText(count));
                       }
                       if (connectServer) {
                         openServer(services, legacy.profile->serverAddress);
                       }
                     });
  }

  void announceLegacyImport(LegacyImport& legacy, QQuickWindow& window) {
    legacy.window = &window;
    if (legacy.imported > 0) {
      showMessage(window, importedText(legacy.imported));
    }
  }

} // namespace jfd::desktop
