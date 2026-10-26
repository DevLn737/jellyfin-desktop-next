#include "IntegrationServices.h"

#include "AppServices.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/integrations/IntegrationsSettings.h>
#include <jfd/integrations/NowPlayingItem.h>
#include <jfd/integrations/ReleaseSignature.h>
#include <jfd/ui/Navigator.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>

#include <type_traits>

Q_LOGGING_CATEGORY(lcIntegrations, "jfd.integrations")

namespace jfd::desktop {

  namespace {
    // MPRIS Identity and the .desktop file (packaging/linux).
    constexpr auto MprisIdentity = "Jellyfin Desktop Next";
    constexpr auto DesktopEntry = "io.github.devln737.JellyfinDesktopNext";

    // Calls a function of the shell (src/ui/shell/AppShell.qml) with text arguments.
    template <typename... Texts>
    void callShell(QQuickWindow& window, const char* function, const Texts&... texts) {
      QObject* shell = window.findChild<QQuickItem*>(QStringLiteral("shell"));
      if (shell != nullptr) QMetaObject::invokeMethod(shell, function, Q_ARG(QVariant, QVariant(texts))...);
    }

    void showDownloadPage(QQuickWindow& window, const QString& version, const QUrl& page) {
      callShell(window, "showLink", QCoreApplication::translate("Integrations", "Version %1 is available.").arg(version),
                QCoreApplication::translate("Integrations", "Download page"), page);
    }

    void offerRestart(QQuickWindow& window, const QString& version) {
      callShell(window, "showLink",
                QCoreApplication::translate("Integrations", "Version %1 is ready to install.").arg(version),
                QCoreApplication::translate("Integrations", "Restart and update"),
                QUrl(integrations::AppImageUpdater::urlScheme() + QStringLiteral(":restart")));
    }

    // After an update the new version opens the screen the old one showed,
    // once the saved session has signed in (home).
    void reopen(ui::Navigator& navigator, const QString& route) {
      const ui::Navigator::Route target = navigator.parse(route);
      if (target.name.isEmpty() || target.name == QStringLiteral("login") || target.name == QStringLiteral("home"))
        return;
      if (navigator.currentName() == QStringLiteral("home")) {
        navigator.navigate(route);
        return;
      }
      auto connection = std::make_shared<QMetaObject::Connection>();
      *connection = QObject::connect(&navigator, &ui::Navigator::currentChanged, &navigator,
                                     [&navigator, route, connection] {
                                       const QString name = navigator.currentName();
                                       if (name == QStringLiteral("home")) navigator.navigate(route);
                                       if (name != QStringLiteral("login")) QObject::disconnect(*connection);
                                     });
    }

    // Downloads and installs updates in place (AppImageUpdater on Linux,
    // MsiUpdater on Windows, the same flow): offered in the toast once
    // downloaded and checked, never while something plays or in a group.
    // Updater: either class; they share the restart link and the start state.
    template <typename Updater>
    void startUpdates(AppServices& services, QQuickWindow& window, Updater& updater) {
      IntegrationServices& integrations = *services.integrations;

      using Kind = typename Updater::StartState::Kind;
      const auto start = updater.confirmStart(QCoreApplication::applicationVersion());
      if (start.kind == Kind::Updated) {
        qCInfo(lcIntegrations) << "Updated to" << start.version;
        callShell(window, "showMessage",
                  QCoreApplication::translate("Integrations", "Updated to version %1.").arg(start.version));
        if (services.navigator != nullptr) reopen(*services.navigator, start.route);
      } else if (start.kind == Kind::RolledBack) {
        qCWarning(lcIntegrations) << "Version" << start.version << "was not installed; this version stays";
        if constexpr (std::is_same_v<Updater, integrations::MsiUpdater>) {
          callShell(window, "showError",
                    QCoreApplication::translate("Integrations", "Version %1 did not install, so this version stays.")
                        .arg(start.version));
        } else {
          callShell(window, "showError",
                    QCoreApplication::translate("Integrations", "Version %1 did not start, so this version stays.")
                        .arg(start.version));
        }
      }

      integrations.updateSupported = [&updater] { return updater.supported(); };
      integrations.prepareUpdate = [&updater](const integrations::Release& release) { updater.prepare(release); };
      integrations.offerReadyUpdate = [&updater] {
        if (!updater.readyVersion().isEmpty()) Q_EMIT updater.ready(updater.readyVersion());
      };
      if (!updater.supported()) return;

      QDesktopServices::setUrlHandler(Updater::urlScheme(), &updater, "handleUrl");
      QObject::connect(&updater, &Updater::ready, &window, [&integrations, &window](const QString& version) {
        qCInfo(lcIntegrations) << "Update" << version << "downloaded and verified";
        integrations.offerWhenIdle = integrations.busy;
        if (!integrations.busy) offerRestart(window, version);
      });
      QObject::connect(&updater, &Updater::failed, &window, [&integrations, &window](const QString& reason) {
        qCInfo(lcIntegrations) << "Update failed:" << reason;
        if (integrations.updates && integrations.updates->available())
          showDownloadPage(window, integrations.updates->available()->version,
                           integrations.updates->available()->page);
      });
      QObject::connect(&updater, &Updater::restartRequested, &window, [&services, &integrations, &window, &updater] {
        if (integrations.busy) {
          integrations.offerWhenIdle = true;
          callShell(window, "showMessage",
                    QCoreApplication::translate(
                        "Integrations", "The update restarts the app, so it waits until playback ends and you are not in a group."));
          return;
        }
        const QString route = services.navigator ? services.navigator->currentRoute() : QString();
        if (updater.apply(route)) {
          qCInfo(lcIntegrations) << "Restarting into" << updater.readyVersion();
          window.close();
        } else {
          callShell(window, "showError",
                    QCoreApplication::translate("Integrations", "The update could not be installed."));
        }
      });
    }

    void startPlatformUpdates(AppServices& services, QQuickWindow& window) {
      IntegrationServices& integrations = *services.integrations;
      const QString state =
          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/update");
#ifdef Q_OS_WIN
      integrations.msi = std::make_unique<integrations::MsiUpdater>(
          &integrations.network, QCoreApplication::applicationFilePath(), integrations::MsiUpdater::installedFolder(),
          state, integrations::releasePublicKey());
      startUpdates(services, window, *integrations.msi);
#else
      integrations.appImage = std::make_unique<integrations::AppImageUpdater>(
          &integrations.network, qEnvironmentVariable("APPIMAGE"), state, integrations::releasePublicKey());
      startUpdates(services, window, *integrations.appImage);
#endif
    }
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
      const bool busy = snapshot.group.has_value() ||
                        (snapshot.phase != core::PlaybackPhase::Idle && snapshot.phase != core::PlaybackPhase::Ended &&
                         snapshot.phase != core::PlaybackPhase::Failed);
      if (integrations.busy && !busy && integrations.offerWhenIdle && integrations.offerReadyUpdate)
        integrations.offerReadyUpdate();
      integrations.busy = busy;
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

    if (!checkForUpdates) return;
    // Also confirms an update that started this version.
    startPlatformUpdates(services, window);
    if (!services.settings->value(integrations::keys::CheckForUpdates).toBool()) return;
    integrations.updates = std::make_unique<integrations::UpdateChecker>(
        &integrations.network, QCoreApplication::applicationVersion(),
        integrations::updateChannel(services.settings->value(integrations::keys::UpdateChannel).toString()));
    auto* checker = integrations.updates.get();
    QObject::connect(checker, &integrations::UpdateChecker::updateAvailable, &window,
                     [&integrations, &window, checker](const QString& version, const QUrl& downloadPage) {
                       if (integrations.updateSupported && integrations.updateSupported() && checker->available()) {
                         integrations.prepareUpdate(*checker->available());
                       } else {
                         showDownloadPage(window, version, downloadPage);
                       }
                     });
    QObject::connect(checker, &integrations::UpdateChecker::checkFailed, checker,
                     [](const QString& reason) { qCInfo(lcIntegrations) << "Update check failed:" << reason; });
    checker->check();
  }

} // namespace jfd::desktop
