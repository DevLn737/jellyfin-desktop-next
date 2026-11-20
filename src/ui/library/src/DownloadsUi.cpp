#include "jfd/ui/DownloadsUi.h"

#include "jfd/ui/Navigator.h"

#include <QCoreApplication>

#include <algorithm>

namespace jfd::ui {

  namespace {
    QString text(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }

    QJsonObject userDataOf(const offline::ItemManifest& m) {
      return !m.userData.isEmpty() ? m.userData : m.item.value(QLatin1String("UserData")).toObject();
    }
  } // namespace

  DownloadsUi::DownloadsUi(QObject* parent) : QObject(parent) {
    s_instance = this;
    connectService();
  }

  DownloadsUi::~DownloadsUi() {
    // The engine deletes the singleton; a later setService must not reach it.
    if (s_instance == this) s_instance = nullptr;
  }

  DownloadsUi* DownloadsUi::create(QQmlEngine*, QJSEngine*) { return new DownloadsUi(); }

  void DownloadsUi::setService(offline::DownloadsController* service) {
    if (s_service != nullptr && s_instance != nullptr) s_service->disconnect(s_instance);
    s_service = service;
    if (s_instance != nullptr) {
      s_instance->connectService();
      Q_EMIT s_instance->changed();
    }
  }

  void DownloadsUi::setTrackListHandler(TrackListHandler handler) {
    s_tracks = std::move(handler);
    if (s_instance != nullptr) Q_EMIT s_instance->changed();
  }

  void DownloadsUi::connectService() {
    m_rowsRevision = -1;
    rebuildRows();
    offline::DownloadsController* service = s_service;
    if (service == nullptr) return;
    using C = offline::DownloadsController;
    const auto changedNow = [this] {
      rebuildRows();
      Q_EMIT changed();
    };
    for (auto signal : {&C::userChanged, &C::onlineChanged, &C::permissionChanged, &C::revisionChanged,
                        &C::queueProblemChanged, &C::storageChanged})
      connect(service, signal, this, changedNow);
    connect(service, &C::progressRevisionChanged, this, &DownloadsUi::progressChanged);
    connect(service, &C::prepared, this, &DownloadsUi::prepared);
    connect(service, &C::prepareFailed, this, &DownloadsUi::prepareFailed);
    connect(service, &C::message, this, &DownloadsUi::message);
  }

  void DownloadsUi::rebuildRows() {
    const int revision = s_service ? s_service->revision() : -2;
    if (revision == m_rowsRevision) return;
    m_rowsRevision = revision;
    QList<library::ItemListModel::Entry> resume, movies, episodes, music, other;
    QList<std::pair<QString, library::ItemListModel::Entry>> started;
    if (s_service != nullptr) {
      for (const offline::ItemManifest& m : s_service->completeItems()) {
        QJsonObject json = m.item;
        const QJsonObject userData = userDataOf(m);
        json.insert(QStringLiteral("UserData"), userData);
        const auto item = api::json::item(json);
        if (!item) continue;
        const library::ItemListModel::Entry entry{*item, s_service->imageUrlOf(m)};
        const QString type = item->type;
        if (type == QLatin1String("Movie")) movies.append(entry);
        else if (type == QLatin1String("Episode")) episodes.append(entry);
        else if (type == QLatin1String("Audio")) music.append(entry);
        else other.append(entry);
        if (type != QLatin1String("Audio") && !item->userData.played &&
            item->userData.playbackPositionTicks > 0)
          started.append({text(userData, "LastPlayedDate"), entry});
      }
    }
    // Most recently watched first.
    std::stable_sort(started.begin(), started.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [when, entry] : std::as_const(started)) resume.append(entry);
    m_resume.setEntries(std::move(resume));
    m_movies.setEntries(std::move(movies));
    m_episodes.setEntries(std::move(episodes));
    m_music.setEntries(std::move(music));
    m_other.setEntries(std::move(other));
  }

  void DownloadsUi::registerRoutes(Navigator& navigator) {
    RouteSpec downloads;
    downloads.name = QStringLiteral("downloads");
    downloads.page = QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Library/DownloadsPage.qml"));
    downloads.hasSidebarEntry = true;
    downloads.sidebar.icon = QStringLiteral("download");
    downloads.sidebar.text = QString::fromUtf8(QT_TRANSLATE_NOOP("Sidebar", "Downloads"));
    downloads.sidebar.translationContext = "Sidebar";
    downloads.sidebar.order = 250;
    downloads.sidebar.placement = RouteSpec::Placement::AccountMenu;
    navigator.registerRoute(downloads);
  }

  QString DownloadsUi::stateOf(const QString& itemId) const {
    return s_service ? s_service->stateOf(itemId) : QString();
  }
  double DownloadsUi::progressOf(const QString& itemId) const {
    return s_service ? s_service->progressOf(itemId) : 0.0;
  }
  QVariantMap DownloadsUi::groupSummary(const QString& parentId) const {
    return s_service ? s_service->groupSummary(parentId) : QVariantMap();
  }
  qint64 DownloadsUi::bytesOf(const QString& itemId) const {
    return s_service ? s_service->bytesOf(itemId) : 0;
  }
  int DownloadsUi::prepare(const QString& itemId, bool unplayedOnly) {
    return s_service ? s_service->prepare(itemId, unplayedOnly) : 0;
  }

  int DownloadsUi::confirm(int requestId, int version) {
    if (s_service == nullptr) return 0;
    const int queued = s_service->confirm(requestId, version);
    if (queued > 0) Q_EMIT message(tr("%n item(s) added to downloads", "", queued), false);
    return queued;
  }

  void DownloadsUi::discard(int requestId) {
    if (s_service) s_service->discard(requestId);
  }
  void DownloadsUi::pause(const QString& itemId) {
    if (s_service) s_service->pause(itemId);
  }
  void DownloadsUi::resume(const QString& itemId) {
    if (s_service) s_service->resume(itemId);
  }
  void DownloadsUi::cancel(const QString& itemId) {
    if (s_service) s_service->cancel(itemId);
  }
  void DownloadsUi::retry(const QString& itemId) {
    if (s_service) s_service->retry(itemId);
  }
  void DownloadsUi::moveUp(const QString& itemId) {
    if (s_service) s_service->moveUp(itemId);
  }
  void DownloadsUi::moveDown(const QString& itemId) {
    if (s_service) s_service->moveDown(itemId);
  }
  void DownloadsUi::pauseAll() {
    if (s_service) s_service->pauseAll();
  }
  void DownloadsUi::resumeAll() {
    if (s_service) s_service->resumeAll();
  }
  bool DownloadsUi::remove(const QString& itemId) { return s_service && s_service->remove(itemId); }
  int DownloadsUi::removeWatched() { return s_service ? s_service->removeWatched() : 0; }
  int DownloadsUi::removeAll() { return s_service ? s_service->removeAll() : 0; }
  QString DownloadsUi::formatBytes(qint64 bytes) const {
    return offline::DownloadsController::formatBytesText(bytes);
  }

  bool DownloadsUi::playTrack(const QString& trackId) {
    if (s_service == nullptr || !s_tracks) return false;
    QList<api::Item> album;
    QString albumId;
    const QList<offline::ItemManifest> items = s_service->completeItems();
    for (const offline::ItemManifest& m : items)
      if (m.itemId == trackId) albumId = text(m.item, "AlbumId");
    int start = 0;
    for (const offline::ItemManifest& m : items) {
      if (text(m.item, "Type") != QLatin1String("Audio")) continue;
      if (m.itemId != trackId && (albumId.isEmpty() || text(m.item, "AlbumId") != albumId)) continue;
      const auto item = api::json::item(m.item);
      if (!item) continue;
      if (m.itemId == trackId) start = int(album.size());
      album.append(*item);
    }
    if (album.isEmpty()) return false;
    return s_tracks(album, start);
  }

} // namespace jfd::ui
