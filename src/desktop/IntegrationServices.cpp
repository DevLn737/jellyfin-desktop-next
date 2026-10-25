#include "IntegrationServices.h"

#include "AppServices.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/integrations/IntegrationsSettings.h>
#include <jfd/integrations/NowPlayingItem.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcIntegrations, "jfd.integrations")

namespace jfd::desktop {

  namespace {
    // MPRIS Identity and the .desktop file (packaging/linux).
    constexpr auto MprisIdentity = "Jellyfin Desktop Next";
    constexpr auto DesktopEntry = "io.github.devln737.JellyfinDesktopNext";
  } // namespace

  void installIntegrations(AppServices& services, QQmlEngine&) {
    services.integrations = std::make_unique<IntegrationServices>();
    IntegrationServices& integrations = *services.integrations;

    integrations.mediaControls = platform::createSystemMediaControls(QString::fromLatin1(MprisIdentity),
                                                                     QString::fromLatin1(DesktopEntry));
    integrations.sleepInhibitor = platform::createSystemSleepInhibitor();
    integrations.bridge = std::make_unique<integrations::MediaSessionBridge>(
        [&integrations](core::SessionInput input) {
          if (integrations.post) integrations.post(std::move(input));
        });
    integrations.bridge->addControls(integrations.mediaControls.get());
    integrations.bridge->setSleepInhibitor(integrations.sleepInhibitor.get());
    integrations.bridge->setPresence(&integrations.presence);

    settings::Settings& settings = *services.settings;
    integrations.presence.configure(settings.value(integrations::keys::Discord).toBool());
    QObject::connect(&settings, &settings::Settings::valueChanged, &integrations.presence,
                     [&integrations](const QString& key, const QVariant& value) {
                       if (key == integrations::keys::Discord) integrations.presence.configure(value.toBool());
                     });
  }

  void connectIntegrations(AppServices& services, session::SessionHub& hub,
                           integrations::MediaSessionBridge::Post post, api::JellyfinClient& client) {
    IntegrationServices& integrations = *services.integrations;
    integrations.post = std::move(post);
    integrations.subscription = hub.subscribe([&integrations, &client](const core::SessionSnapshot& snapshot) {
      const QString item = QString::fromStdString(snapshot.item.value());
      if (!item.isEmpty() && item != integrations.detailsItem) {
        integrations.detailsItem = item;
        client.item(item, integrations.bridge.get(), [&integrations, &client](api::Result<api::Item> result) {
          if (!result) {
            qCInfo(lcIntegrations) << "No details for the item that plays:" << result.error().message;
            return;
          }
          integrations.bridge->setDetails(integrations::nowPlayingDetails(*result, client));
        });
      }
      integrations.bridge->update(snapshot);
    });
  }

  void startIntegrations(AppServices& services, QQuickWindow& window, bool checkForUpdates) {
    IntegrationServices& integrations = *services.integrations;
    platform::MediaControls& controls = *integrations.mediaControls;
    controls.attachWindow(&window);
    QObject::connect(&controls, &platform::MediaControls::raiseRequested, &window, [&window] {
      window.show();
      window.raise();
      window.requestActivate();
    });
    // The same way out as closing the window.
    QObject::connect(&controls, &platform::MediaControls::quitRequested, &window, &QQuickWindow::close);

    if (!checkForUpdates || !services.settings->value(integrations::keys::CheckForUpdates).toBool()) return;
    integrations.updates = std::make_unique<integrations::UpdateChecker>(
        &integrations.network, QCoreApplication::applicationVersion(),
        integrations::updateChannel(services.settings->value(integrations::keys::UpdateChannel).toString()));
    auto* checker = integrations.updates.get();
    QObject::connect(checker, &integrations::UpdateChecker::updateAvailable, &window,
                     [&window](const QString& version, const QUrl& downloadPage) {
                       QObject* shell = window.findChild<QQuickItem*>(QStringLiteral("shell"));
                       if (shell == nullptr) return;
                       QMetaObject::invokeMethod(
                           shell, "showLink",
                           Q_ARG(QVariant, QCoreApplication::translate("Integrations", "Version %1 is available.").arg(version)),
                           Q_ARG(QVariant, QCoreApplication::translate("Integrations", "Download page")),
                           Q_ARG(QVariant, downloadPage));
                     });
    QObject::connect(checker, &integrations::UpdateChecker::checkFailed, checker,
                     [](const QString& reason) { qCInfo(lcIntegrations) << "Update check failed:" << reason; });
    checker->check();
  }

} // namespace jfd::desktop
