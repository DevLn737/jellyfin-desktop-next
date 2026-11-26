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
#include <utility>

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
      integrations.readyVersion = [&updater] { return updater.readyVersion(); };
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
        qCInfo(lcIntegrations) << "Restart and update chosen, ready:" << updater.readyVersion()
                               << "busy:" << integrations.busy;
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
          // The installer waits for this process to end. Quit, not only
          // close the window: another top-level window (picture in picture)
          // would keep the program, and so the installer, waiting.
          window.close();
          QCoreApplication::quit();
        } else {
          qCWarning(lcIntegrations) << "Could not start the update of" << updater.readyVersion();
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
          state, integrations::releasePublicKeys());
      startUpdates(services, window, *integrations.msi);
#else
      integrations.appImage = std::make_unique<integrations::AppImageUpdater>(
          &integrations.network, qEnvironmentVariable("APPIMAGE"), state, integrations::releasePublicKeys());
      startUpdates(services, window, *integrations.appImage);
#endif
    }
    integrations::PresencePrivacy presencePrivacy(const settings::Settings& settings) {
      using namespace integrations;
      PresencePrivacy privacy;
      privacy.hidden = settings.value(keys::DiscordHidden).toBool();
      privacy.showTitle = settings.value(keys::DiscordDetail).toString() != QLatin1String("kind");
      privacy.movies = settings.value(keys::DiscordMovies).toBool();
      privacy.series = settings.value(keys::DiscordSeries).toBool();
      privacy.music = settings.value(keys::DiscordMusic).toBool();
      privacy.liveTv = settings.value(keys::DiscordLiveTv).toBool();
      for (const QString& id :
           settings.value(keys::DiscordHiddenLibraries).toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        privacy.hiddenLibraries.insert(id.trimmed());
      return privacy;
    }

    // Changes the details of the item that plays, if it still plays, and
    // publishes them.
    void amendDetails(IntegrationServices& integrations, const QString& item,
                      const std::function<void(integrations::NowPlayingDetails&)>& change) {
      if (!integrations.details || integrations.detailsItem != item) return;
      change(*integrations.details);
      integrations.bridge->setDetails(*integrations.details);
    }

    // What Discord needs beyond the item: its library (to hide it), its
    // series' links, and a poster on a public host. Asked only while the
    // card is on.
    void fetchPresenceDetails(IntegrationServices& integrations, const settings::Settings& settings,
                              api::JellyfinClient& client, const api::Item& item) {
      if (!settings.value(integrations::keys::Discord).toBool()) return;
      QObject* context = integrations.bridge.get();
      const QString id = item.id;
      client.ancestors(id, context, [&integrations, id](api::Result<api::ItemPage> result) {
        if (!result) {
          qCInfo(lcIntegrations) << "No library for the item that plays:" << result.error().message;
          return;
        }
        QStringList ids;
        for (const api::Item& folder : result->items) ids.append(folder.id);
        amendDetails(integrations, id, [&ids](integrations::NowPlayingDetails& details) {
          details.libraryIds = ids;
          details.librariesKnown = true;
        });
      });
      if (item.type == QLatin1String("Episode") && !item.seriesId.isEmpty())
        client.item(item.seriesId, context, [&integrations, id](api::Result<api::Item> series) {
          if (!series) return;
          const auto links = integrations::presenceLinks(integrations::PresenceInfo::Kind::Episode, series->providerIds);
          amendDetails(integrations, id, [&links](integrations::NowPlayingDetails& details) { details.links = links; });
        });
      if (item.type != QLatin1String("TvChannel") && settings.value(integrations::keys::DiscordPoster).toBool())
        client.remoteImage(integrations::posterItemId(item), QStringLiteral("Primary"), context,
                           [&integrations, id](api::Result<QUrl> poster) {
                             if (!poster || poster->isEmpty()) return;
                             const QUrl url = *poster;
                             amendDetails(integrations, id, [&url](integrations::NowPlayingDetails& details) {
                               details.externalPoster = url;
                             });
                           });
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
    integrations.presence.setShowPoster(settings.value(integrations::keys::DiscordPoster).toBool());
    integrations.presence.setPrivacy(presencePrivacy(settings));
    integrations.presence.configure(settings.value(integrations::keys::Discord).toBool());
    QObject::connect(&settings, &settings::Settings::valueChanged, &integrations.presence,
                     [&integrations, &settings](const QString& key, const QVariant& value) {
                       if (key == integrations::keys::Discord) integrations.presence.configure(value.toBool());
                       if (key == integrations::keys::DiscordPoster)
                         integrations.presence.setShowPoster(value.toBool());
                       if (key.startsWith(QLatin1String("integrations/discord")))
                         integrations.presence.setPrivacy(presencePrivacy(settings));
                     });
  }

  void refreshDetails(AppServices& services) {
    if (!services.integrations || !services.integrations->details || !services.integrations->bridge) return;
    IntegrationServices& integrations = *services.integrations;
    if (integrations.completeDetails) integrations.completeDetails(*integrations.details);
    integrations.bridge->setDetails(*integrations.details);
  }

  void connectIntegrations(AppServices& services, session::SessionHub& hub,
                           integrations::MediaSessionBridge::Post post, api::JellyfinClient& client) {
    IntegrationServices& integrations = *services.integrations;
    integrations.post = std::move(post);
    const settings::Settings& settings = *services.settings;
    integrations.subscription = hub.subscribe([&integrations, &client, &settings](const core::SessionSnapshot& snapshot) {
      const bool busy = snapshot.group.has_value() ||
                        (snapshot.phase != core::PlaybackPhase::Idle && snapshot.phase != core::PlaybackPhase::Ended &&
                         snapshot.phase != core::PlaybackPhase::Failed);
      if (integrations.busy && !busy && integrations.offerWhenIdle && integrations.offerReadyUpdate)
        integrations.offerReadyUpdate();
      integrations.busy = busy;
      const QString item = QString::fromStdString(snapshot.item.value());
      if (!item.isEmpty() && item != integrations.detailsItem) {
        integrations.detailsItem = item;
        client.item(item, integrations.bridge.get(), [&integrations, &client, &settings](api::Result<api::Item> result) {
          if (!result) {
            qCInfo(lcIntegrations) << "No details for the item that plays:" << result.error().message;
            return;
          }
          integrations::NowPlayingDetails details = integrations::nowPlayingDetails(*result, client);
          if (integrations.completeDetails) integrations.completeDetails(details);
          integrations.details = details;
          integrations.bridge->setDetails(std::move(details));
          fetchPresenceDetails(integrations, settings, client, *result);
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
    settings::Settings& settings = *services.settings;
    integrations.updates = std::make_unique<integrations::UpdateChecker>(
        &integrations.network, QCoreApplication::applicationVersion(),
        integrations::updateChannel(settings.value(integrations::keys::UpdateChannel).toString()));
    auto* checker = integrations.updates.get();
    QObject::connect(checker, &integrations::UpdateChecker::updateAvailable, &window,
                     [&integrations, &window, checker](const QString& version, const QUrl& downloadPage) {
                       const bool manual = std::exchange(integrations.manualCheck, false);
                       if (integrations.updateSupported && integrations.updateSupported() && checker->available()) {
                         if (manual) {
                           // Already downloaded: offer it again, even while
                           // something plays (the link then says it waits).
                           if (integrations.readyVersion && integrations.readyVersion() == version) {
                             offerRestart(window, version);
                             return;
                           }
                           callShell(window, "showMessage",
                                     QCoreApplication::translate(
                                         "Integrations",
                                         "Version %1 is available. It is downloading; when it is ready, you can "
                                         "restart and update.")
                                         .arg(version));
                         }
                         integrations.prepareUpdate(*checker->available());
                       } else {
                         showDownloadPage(window, version, downloadPage);
                       }
                     });
    QObject::connect(checker, &integrations::UpdateChecker::upToDate, &window, [&integrations, &window] {
      if (!std::exchange(integrations.manualCheck, false)) return;
      callShell(window, "showMessage",
                QCoreApplication::translate("Integrations", "You have the latest version (%1).")
                    .arg(QCoreApplication::applicationVersion()));
    });
    QObject::connect(checker, &integrations::UpdateChecker::checkFailed, &window,
                     [&integrations, &window](const QString& reason) {
                       qCInfo(lcIntegrations) << "Update check failed:" << reason;
                       if (!std::exchange(integrations.manualCheck, false)) return;
                       callShell(window, "showError",
                                 QCoreApplication::translate(
                                     "Integrations",
                                     "Could not check for updates. Check the internet connection and try again."));
                     });
    // "Check for updates" in the settings, in the channel chosen now.
    QObject::connect(&settings, &settings::Settings::actionTriggered, &window,
                     [&integrations, &settings, &window, checker](const QString& key) {
                       if (key != integrations::keys::CheckNow) return;
                       integrations.manualCheck = true;
                       if (checker->checking()) return;
                       callShell(window, "showMessage",
                                 QCoreApplication::translate("Integrations", "Checking for updates…"));
                       checker->setChannel(integrations::updateChannel(
                           settings.value(integrations::keys::UpdateChannel).toString()));
                       checker->check();
                     });
    if (settings.value(integrations::keys::CheckForUpdates).toBool()) checker->check();
  }

} // namespace jfd::desktop
