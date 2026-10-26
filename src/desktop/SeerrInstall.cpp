#include "AppServices.h"

#include <jfd/diagnostics/Redact.h>
#include <jfd/platform/CredentialStore.h>
#include <jfd/seerr/SeerrService.h>
#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/RequestsUi.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>

namespace jfd::desktop {

  namespace {
    const QString AddressKey = QStringLiteral("seerr/address");
    const QString SignInKey = QStringLiteral("seerr/signIn");
    const QString CheckKey = QStringLiteral("seerr/check");
    const QString SignOutKey = QStringLiteral("seerr/signOut");

    // The shell's toast; the window exists by the time a message is sent.
    void toast(QQmlEngine& engine, const QString& text, bool error) {
      auto* app = qobject_cast<QQmlApplicationEngine*>(&engine);
      if (app == nullptr || app->rootObjects().isEmpty()) return;
      QObject* shell = app->rootObjects().first()->findChild<QObject*>(QStringLiteral("shell"));
      if (shell == nullptr) return;
      QMetaObject::invokeMethod(shell, error ? "showError" : "showMessage", Q_ARG(QVariant, QVariant(text)));
    }

    // Keeps the address out of logs and reports whatever it is set to.
    void protectAddress(const seerr::SeerrService& service) {
      diagnostics::clearProtectedAddresses();
      if (service.configured()) {
        diagnostics::addProtectedAddress(service.serverUrl().toString().toStdString());
      }
    }
  } // namespace

  bool registerSeerrSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("seerr");
    section.context = "SeerrSettings";
    section.title = QT_TRANSLATE_NOOP("SeerrSettings", "Requests (Seerr)");
    section.order = 125;

    settings::SettingDescriptor address;
    address.key = QStringLiteral("address");
    address.type = settings::SettingType::String;
    address.defaultValue = QString();
    address.label = QT_TRANSLATE_NOOP("SeerrSettings", "Seerr address");
    address.description = QT_TRANSLATE_NOOP(
        "SeerrSettings",
        "The address of your Seerr server, for example https://seerr.example.org. Empty: no Requests "
        "in the sidebar and nothing from Seerr on the home page. You sign in with your Jellyfin "
        "account; the password is not saved.");
    section.settings.append(address);

    const auto action = [&section](const char* key, const char* label) {
      settings::SettingDescriptor setting;
      setting.key = QLatin1String(key);
      setting.type = settings::SettingType::Action;
      setting.label = label;
      section.settings.append(setting);
    };
    action("signIn", QT_TRANSLATE_NOOP("SeerrSettings", "Sign in to Seerr"));
    action("check", QT_TRANSLATE_NOOP("SeerrSettings", "Check the connection"));
    action("signOut", QT_TRANSLATE_NOOP("SeerrSettings", "Sign out of Seerr"));
    return registry.registerSection(std::move(section));
  }

  void installSeerr(AppServices& services, QQmlEngine& engine) {
    // A profile of its own (the smoke test) has no credential storage.
    services.seerrCredentials = services.isolatedProfile ? platform::createUnavailableCredentialStore()
                                                         : platform::createSystemCredentialStore();
    services.seerr = std::make_unique<seerr::SeerrService>(*services.seerrCredentials);
    seerr::SeerrService& service = *services.seerr;
    settings::Settings& store = *services.settings;

    ui::RequestsUi::setService(&service);
    ui::RequestsUi::registerRoutes(*services.navigator);

    const auto apply = [&service, &store] {
      service.setAddress(store.value(AddressKey).toString());
      protectAddress(service);
    };
    apply();
    QObject::connect(&store, &settings::Settings::valueChanged, &service, [apply](const QString& key) {
      if (key == AddressKey) apply();
    });

    ui::Navigator* navigator = services.navigator;
    QObject::connect(&store, &settings::Settings::actionTriggered, &service,
                     [&service, &engine, navigator](const QString& key) {
                       if (key == SignInKey) {
                         if (!service.configured()) {
                           toast(engine, ui::RequestsUi::outcomeTextFor(int(seerr::Outcome::NoAddress)), true);
                         } else {
                           navigator->navigate(QStringLiteral("seerr-signin"));
                         }
                       } else if (key == CheckKey) {
                         service.check();
                       } else if (key == SignOutKey) {
                         service.signOut();
                         if (service.configured()) toast(engine, QCoreApplication::translate("SeerrSettings", "Signed out of Seerr."), false);
                       }
                     });
    QObject::connect(&service, &seerr::SeerrService::checkFinished, &service,
                     [&engine](int outcome, const QString& userName) {
                       if (outcome == int(seerr::Outcome::Ok)) {
                         toast(engine, QCoreApplication::translate("SeerrSettings", "Connected to Seerr as %1.").arg(userName), false);
                       } else {
                         toast(engine, ui::RequestsUi::outcomeTextFor(outcome), true);
                       }
                     });
  }

  void uninstallSeerr(AppServices& services) {
    // The QML singleton reads the service until the engine is gone.
    ui::RequestsUi::setService(nullptr);
    services.seerr.reset();
    services.seerrCredentials.reset();
    diagnostics::clearProtectedAddresses();
  }

} // namespace jfd::desktop
