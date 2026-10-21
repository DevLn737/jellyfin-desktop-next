#include "AppServices.h"

#include <jfd/settings/CommonSections.h>
#include <jfd/ui/LibraryScreens.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QScreen>
#include <QSysInfo>

namespace jfd::desktop {

  namespace {
    const QString DeviceIdKey = QStringLiteral("library/deviceId");

    api::ClientIdentity identity(settings::Settings& settings) {
      api::ClientIdentity identity;
      identity.version = QCoreApplication::applicationVersion();
      identity.device = QSysInfo::machineHostName();
      if (identity.device.isEmpty()) identity.device = QSysInfo::prettyProductName();
      // The server keys sessions and tokens by the device id, so it is made
      // once per profile.
      identity.deviceId = settings.value(DeviceIdKey).toString();
      if (identity.deviceId.isEmpty()) {
        identity.deviceId = api::generateDeviceId();
        settings.setValue(DeviceIdKey, identity.deviceId);
      }
      return identity;
    }
  } // namespace

  struct LibraryServices {
    explicit LibraryServices(ui::LibraryScreens::Config config) : screens(std::move(config)) {}
    ui::LibraryScreens screens;
  };

  bool registerLibrarySettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("library");
    section.context = "LibrarySettings";
    section.title = "library";
    section.hidden = true;

    settings::SettingDescriptor deviceId;
    deviceId.key = QStringLiteral("deviceId");
    deviceId.type = settings::SettingType::String;
    deviceId.defaultValue = QString();
    deviceId.label = "deviceId";
    section.settings.append(deviceId);
    return registry.registerSection(std::move(section));
  }

  void installLibrary(AppServices& services, QQmlEngine& engine) {
    ui::LibraryScreens::Config config;
    config.identity = identity(*services.settings);
    // Saved servers sit next to the settings, so the smoke test's temporary
    // profile has its own.
    config.serversFile =
        QFileInfo(services.settings->filePath()).dir().filePath(QStringLiteral("servers.ini"));
    if (!services.cacheDirectory.isEmpty())
      config.imageDirectory = services.cacheDirectory + QStringLiteral("/images");
    config.tokens = services.isolatedProfile ? platform::createUnavailableCredentialStore()
                                             : platform::createSystemCredentialStore();
    if (const QScreen* screen = QGuiApplication::primaryScreen())
      config.devicePixelRatio = screen->devicePixelRatio();

    services.library = std::make_shared<LibraryServices>(std::move(config));
    ui::LibraryScreens& screens = services.library->screens;
    services.client = &screens.client();
    services.socket = &screens.socket();
    const bool installed = screens.install(engine, *services.navigator);
    Q_ASSERT(installed);
    Q_UNUSED(installed);
    services.libraryUi = screens.ui();

    // Messages for the shell's toast; the window exists by the time one is
    // sent.
    QObject::connect(services.libraryUi, &ui::LibraryUi::message, &engine,
                     [&engine](const QString& text, bool error) {
                       auto* app = qobject_cast<QQmlApplicationEngine*>(&engine);
                       if (app == nullptr || app->rootObjects().isEmpty()) return;
                       QObject* shell =
                           app->rootObjects().first()->findChild<QObject*>(QStringLiteral("shell"));
                       if (shell == nullptr) return;
                       QMetaObject::invokeMethod(shell, error ? "showError" : "showMessage",
                                                 Q_ARG(QVariant, QVariant(text)));
                     });

    QObject::connect(services.settings.get(), &settings::Settings::actionTriggered,
                     services.navigator, [&screens](const QString& key) {
                       if (key == settings::keys::SignOut) {
                         screens.signIn().signOut();
                       } else if (key == settings::keys::ChangeServer) {
                         screens.signIn().signOut();
                         screens.signIn().back();
                       }
                     });

    // Signs in with the saved session, which opens the home page; otherwise
    // main.cpp opens the login page.
    screens.signIn().restore();
  }

} // namespace jfd::desktop
