#include "AppServices.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/library/SignIn.h>
#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadSettings.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/DownloadsController.h>
#include <jfd/offline/ExtrasDownloader.h>
#include <jfd/offline/JournalReportSink.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/OfflineLocalSource.h>
#include <jfd/offline/OfflineSync.h>
#include <jfd/offline/StorageGuard.h>
#include <jfd/offline/ViewJournal.h>
#include <jfd/music/MusicPlayer.h>
#include <jfd/music/MusicQueue.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/DownloadsUi.h>
#include <jfd/ui/Navigator.h>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTimer>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcDownloads, "jfd.app.downloads")

    // While the server can be reached, the downloads' watched marks are
    // refreshed this often (and at every reconnection).
    constexpr int RefreshEveryMs = 30 * 60 * 1000;
    constexpr std::int64_t TicksPerMs = 10'000;
  } // namespace

  // Offline downloads (stage 3.10a): the folder and its catalog, the queue
  // and the extras, the local copies the launcher plays, the watch journal
  // and its sync with the server.
  struct DownloadServices {
    explicit DownloadServices(const QString& root, const QString& journalPath)
        : store(root), catalog(store), guard(std::make_shared<offline::StorageGuard>()),
          queue(store, catalog, guard), extras(store, catalog), local(store, catalog),
          journal(journalPath), controller(store, catalog, queue, *guard) {}

    offline::DownloadStore store;
    offline::OfflineCatalog catalog;
    std::shared_ptr<offline::StorageGuard> guard;
    offline::DownloadQueue queue;
    offline::ExtrasDownloader extras;
    offline::OfflineLocalSource local;
    offline::ViewJournal journal;
    // The screens' lists and actions (the `Downloads` QML singleton).
    offline::DownloadsController controller;
    std::unique_ptr<offline::OfflineSync> sync;
    QTimer refresh;
    QString serverId;
    QString userId;
    // "Continue downloads when the app starts" is off: the first sign-in
    // of this run pauses what was left unfinished.
    bool pauseLeftovers = false;
  };

  namespace {
    void applyLimits(DownloadServices& downloads, const settings::Settings& settings) {
      downloads.guard->setLimitBytes(settings.value(offline::keys::MaxGiB).toLongLong() * offline::GiB);
      downloads.guard->setReserveBytes(settings.value(offline::keys::ReserveGiB).toLongLong() *
                                       offline::GiB);
    }

    // The signed-in user's downloads run and play; another user's stay on
    // disk, hidden, until that user signs in again.
    void applyAccess(AppServices& services) {
      DownloadServices& downloads = *services.downloads;
      api::JellyfinClient& client = *services.client;
      const bool signedIn = services.signIn->step() == library::SignIn::Step::SignedIn &&
                            client.isSignedIn();
      downloads.serverId = signedIn ? services.signIn->serverId() : QString();
      downloads.userId = signedIn ? client.userId() : QString();
      const QUrl serverUrl = signedIn ? client.serverUrl() : QUrl();
      const QByteArray authorization = signedIn ? client.authorizationHeader() : QByteArray();
      downloads.queue.setAccess(downloads.serverId, downloads.userId, serverUrl, authorization);
      downloads.extras.setAccess(downloads.serverId, downloads.userId, serverUrl, authorization);
      downloads.local.setUser(downloads.serverId, downloads.userId);
      downloads.sync->setUser(downloads.serverId, downloads.userId);
      downloads.controller.setUser(downloads.serverId, downloads.userId);
      if (signedIn && downloads.pauseLeftovers) {
        downloads.pauseLeftovers = false;
        for (const QString& itemId : downloads.queue.order()) downloads.queue.pause(itemId);
      }
    }

    bool online(const AppServices& services) {
      return services.socket->state() == api::SessionSocket::State::Ready;
    }
  } // namespace

  bool registerDownloadSettings(settings::SettingsRegistry& registry) {
    return offline::registerDownloadSettings(registry);
  }

  void installDownloads(AppServices& services) {
    Q_ASSERT(services.session && services.client != nullptr && services.socket != nullptr &&
             services.signIn != nullptr);
    settings::Settings& settings = *services.settings;
    // The smoke test's throwaway profile keeps its downloads and journal
    // inside it; a real profile uses the setting and the app's data folder.
    QString root;
    QString journal;
    if (services.isolatedProfile) {
      const QString profile = QFileInfo(services.logDirectory).absolutePath();
      root = profile + QStringLiteral("/downloads");
      journal = profile + QStringLiteral("/offline-journal.jsonl");
    } else {
      root = QDir::fromNativeSeparators(settings.value(offline::keys::Folder).toString());
      if (root.isEmpty()) root = offline::defaultDownloadFolder();
      journal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                QStringLiteral("/offline-journal.jsonl");
    }
    services.downloads = std::make_shared<DownloadServices>(root, journal);
    DownloadServices& downloads = *services.downloads;
    downloads.sync = std::make_unique<offline::OfflineSync>(*services.client, downloads.journal,
                                                           downloads.store, downloads.catalog);
    const qsizetype count = downloads.catalog.load();
    qCInfo(lcDownloads) << count << "downloaded items in the catalog";
    applyLimits(downloads, settings);
    downloads.local.setPreferLocal(settings.value(offline::keys::PreferLocal).toBool());
    downloads.local.setOnline(online(services));
    downloads.queue.setMaxParallel(settings.value(offline::keys::MaxParallel).toInt());
    downloads.pauseLeftovers = !settings.value(offline::keys::ResumeOnStart).toBool();
    downloads.controller.setQuality(settings.value(offline::keys::Quality).toString());
    downloads.controller.setClient(services.client);
    downloads.controller.setOnline(online(services));

    // Playing goes to the copy on disk first (see LocalSource).
    services.session->launcher().setLocalSource(&downloads.local);

    QObject::connect(services.settings.get(), &settings::Settings::valueChanged, &downloads.queue,
                     [&services](const QString& key, const QVariant& value) {
                       DownloadServices& d = *services.downloads;
                       if (key == offline::keys::MaxGiB || key == offline::keys::ReserveGiB)
                         applyLimits(d, *services.settings);
                       else if (key == offline::keys::PreferLocal)
                         d.local.setPreferLocal(value.toBool());
                       else if (key == offline::keys::MaxParallel)
                         d.queue.setMaxParallel(value.toInt());
                       else if (key == offline::keys::Quality)
                         d.controller.setQuality(value.toString());
                       if (key == offline::keys::MaxGiB || key == offline::keys::ReserveGiB)
                         d.controller.refresh();
                     });

    library::SignIn& signIn = *services.signIn;
    QObject::connect(&signIn, &library::SignIn::signedIn, &downloads.queue, [&services] {
      applyAccess(services);
      services.downloads->extras.fetchMissing();
    });
    // Signing out keeps the downloads (the default the owner chose); they
    // are hidden until the same user signs in again.
    QObject::connect(&signIn, &library::SignIn::signedOut, &downloads.queue,
                     [&services] { applyAccess(services); });
    // The client gets a new token in place (a second sign-in): the queue
    // needs it.
    QObject::connect(services.client, &api::JellyfinClient::sessionChanged, &downloads.queue,
                     [&services] {
                       if (services.signIn->step() == library::SignIn::Step::SignedIn) applyAccess(services);
                     });

    QObject::connect(services.socket, &api::SessionSocket::stateChanged, &downloads.queue,
                     [&services](api::SessionSocket::State state) {
                       DownloadServices& d = *services.downloads;
                       const bool ready = state == api::SessionSocket::State::Ready;
                       d.local.setOnline(ready);
                       d.controller.setOnline(ready);
                       if (!ready) return;
                       // Back in touch: what was watched offline goes out,
                       // watched marks from other devices come in.
                       d.sync->syncNow();
                       d.extras.fetchMissing();
                     });
    downloads.refresh.setInterval(RefreshEveryMs);
    QObject::connect(&downloads.refresh, &QTimer::timeout, &downloads.queue, [&services] {
      if (online(services)) services.downloads->sync->syncNow();
    });
    downloads.refresh.start();

    // Watched marks from the server and fetched images change what the
    // screens show.
    QObject::connect(downloads.sync.get(), &offline::OfflineSync::finished, &downloads.controller,
                     [&services] { services.downloads->controller.refresh(); });
    QObject::connect(&downloads.extras, &offline::ExtrasDownloader::finished, &downloads.controller,
                     [&services] { services.downloads->controller.refresh(); });

    // A finished file gets its subtitles, images, lyrics and segments.
    QObject::connect(&downloads.queue, &offline::DownloadQueue::completed, &downloads.queue,
                     [&services](const QString& itemId) { services.downloads->extras.fetch(itemId); });

    if (signIn.step() == library::SignIn::Step::SignedIn) {
      applyAccess(services);
      if (online(services)) {
        downloads.sync->syncNow();
        downloads.extras.fetchMissing();
      }
    }
  }

  std::unique_ptr<playback::ReportSink> downloadsReportSink(AppServices& services) {
    if (!services.downloads) return {};
    offline::JournalReportSink::Hooks hooks;
    hooks.online = [&services] { return online(services); };
    hooks.serverId = [&services] { return services.downloads ? services.downloads->serverId : QString(); };
    hooks.userId = [&services] { return services.downloads ? services.downloads->userId : QString(); };
    // Server minus system clock from SyncPlay's TimeSync measurement.
    hooks.clockOffsetMs = [&services]() -> std::optional<std::int64_t> {
      if (!services.session) return std::nullopt;
      const auto offset = services.session->group().clockOffset();
      if (!offset) return std::nullopt;
      const std::int64_t serverNowMs =
          offset->toServer(services.session->clock().now()).sinceOrigin().count() / TicksPerMs;
      return serverNowMs - QDateTime::currentMSecsSinceEpoch();
    };
    return std::make_unique<offline::JournalReportSink>(*services.client, services.downloads->journal,
                                                        std::move(hooks));
  }

  std::function<std::optional<api::Lyrics>(const QString&)> downloadsLyrics(AppServices& services) {
    return [&services](const QString& trackId) -> std::optional<api::Lyrics> {
      if (!services.downloads) return std::nullopt;
      return services.downloads->local.lyrics(trackId);
    };
  }

  void installDownloadScreens(AppServices& services, QQmlEngine& engine) {
    if (!services.downloads) return;
    ui::DownloadsUi::setService(&services.downloads->controller);
    ui::DownloadsUi::registerRoutes(*services.navigator);
    // Downloaded tracks play in the music player without the server.
    ui::DownloadsUi::setTrackListHandler([&services](const QList<api::Item>& items, int start) {
      if (!services.music || !services.music->available()) return false;
      QList<music::Track> tracks;
      for (const api::Item& item : items) tracks.append(music::Track::fromItem(item));
      services.music->play(std::move(tracks), start);
      return true;
    });
    // Messages for the shell's toast, as the library's.
    auto* ui = engine.singletonInstance<ui::DownloadsUi*>("Jfd.Ui.Library", "Downloads");
    if (ui == nullptr) return;
    QObject::connect(ui, &ui::DownloadsUi::message, &engine, [&engine](const QString& text, bool error) {
      auto* app = qobject_cast<QQmlApplicationEngine*>(&engine);
      if (app == nullptr || app->rootObjects().isEmpty()) return;
      QObject* shell = app->rootObjects().first()->findChild<QObject*>(QStringLiteral("shell"));
      if (shell == nullptr) return;
      QMetaObject::invokeMethod(shell, error ? "showError" : "showMessage", Q_ARG(QVariant, QVariant(text)));
    });
  }

  void uninstallDownloads(AppServices& services) {
    ui::DownloadsUi::setTrackListHandler({});
    ui::DownloadsUi::setService(nullptr);
    if (services.session) services.session->launcher().setLocalSource(nullptr);
    services.downloads.reset();
  }

} // namespace jfd::desktop
