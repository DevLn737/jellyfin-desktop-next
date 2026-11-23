#include <jfd/offline/DownloadsController.h>

#include <jfd/api/Downloads.h>
#include <jfd/offline/CompressedDownload.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSet>

#include <algorithm>

namespace jfd::offline {

  namespace {
    QString text(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }

    std::int64_t number(const QJsonObject& o, const char* key) {
      return static_cast<std::int64_t>(o.value(QLatin1String(key)).toDouble());
    }

    // Items a download takes as they are; folders take their children.
    bool playable(const QJsonObject& item) {
      if (item.value(QLatin1String("IsFolder")).toBool()) return false;
      static const QSet<QString> types{
          QStringLiteral("Movie"),      QStringLiteral("Episode"), QStringLiteral("Video"),
          QStringLiteral("MusicVideo"), QStringLiteral("Audio"),   QStringLiteral("AudioBook"),
      };
      return types.contains(text(item, "Type"));
    }

    QJsonObject userDataOf(const ItemManifest& m) {
      if (!m.userData.isEmpty()) return m.userData;
      return m.item.value(QLatin1String("UserData")).toObject();
    }

    // The ids a download is listed under besides its own.
    QStringList groupIdsOf(const ItemManifest& m) {
      QStringList ids;
      for (const char* key : {"SeriesId", "SeasonId", "AlbumId"}) {
        const QString id = text(m.item, key);
        if (!id.isEmpty() && id != m.itemId) ids.append(id);
      }
      for (const QString& parent : m.parents)
        if (!ids.contains(parent)) ids.append(parent);
      return ids;
    }

    // Sorts downloads by series or album, then season and number, then name.
    QString sortKey(const ItemManifest& m) {
      const QString group = !text(m.item, "SeriesName").isEmpty() ? text(m.item, "SeriesName")
                                                                  : text(m.item, "Album");
      const auto pad = [](std::int64_t n) { return QStringLiteral("%1").arg(n, 6, 10, QLatin1Char('0')); };
      return (group.isEmpty() ? text(m.item, "Name") : group).toLower() + QLatin1Char('\x1f') +
             pad(number(m.item, "ParentIndexNumber")) + pad(number(m.item, "IndexNumber")) +
             text(m.item, "Name").toLower();
    }

    // The nearest folder that exists: the download folder may not yet.
    QString existingFolder(QString path) {
      for (int i = 0; i < 64 && !path.isEmpty(); ++i) {
        if (QFileInfo(path).isDir()) return path;
        const QString up = QFileInfo(path).absolutePath();
        if (up == path) break;
        path = up;
      }
      return path;
    }
  } // namespace

  DownloadsController::DownloadsController(DownloadStore& store, OfflineCatalog& catalog,
                                           DownloadQueue& queue, StorageGuard& guard,
                                           QObject* parent)
      : QObject(parent), m_store(store), m_catalog(catalog), m_queue(queue), m_guard(guard) {
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(0);
    connect(&m_rebuild, &QTimer::timeout, this, &DownloadsController::rebuild);
    connect(&m_queue, &DownloadQueue::itemChanged, this, &DownloadsController::scheduleRebuild);
    connect(&m_queue, &DownloadQueue::orderChanged, this, &DownloadsController::scheduleRebuild);
    connect(&m_queue, &DownloadQueue::completed, this, &DownloadsController::scheduleRebuild);
    connect(&m_queue, &DownloadQueue::failed, this, &DownloadsController::scheduleRebuild);
    connect(&m_queue, &DownloadQueue::problemChanged, this, [this] {
      Q_EMIT queueProblemChanged();
      scheduleRebuild();
    });
    connect(&m_queue, &DownloadQueue::progress, this,
            [this](const QString& itemId, qint64 bytesOnDisk, qint64 total) {
              const auto member = m_members.constFind(itemId);
              if (member == m_members.cend()) return;
              const auto status = queuedStatus(itemId);
              m_queueModel.updateProgress(itemId, bytesOnDisk, total,
                                          statusTextFor(*member, status ? &*status : nullptr));
              ++m_progressRevision;
              Q_EMIT progressRevisionChanged();
            });
  }

  DownloadsController::~DownloadsController() = default;

  std::optional<DownloadStatus> DownloadsController::queuedStatus(const QString& itemId) const {
    auto status = m_queue.status(itemId);
    if (status && status->state == ItemState::Complete) return std::nullopt;
    return status;
  }

  void DownloadsController::setClient(api::JellyfinClient* client) {
    m_client = client;
    askPermission();
  }

  void DownloadsController::setUser(const QString& serverId, const QString& userId) {
    if (serverId == m_serverId && userId == m_userId) return;
    m_serverId = serverId;
    m_userId = userId;
    m_plans.clear();
    ++m_permissionAttempt;
    setPermission(false, false);
    Q_EMIT userChanged();
    rebuild();
    askPermission();
  }

  void DownloadsController::setOnline(bool online) {
    if (online == m_online) return;
    m_online = online;
    Q_EMIT onlineChanged();
    if (online && !m_permissionKnown) askPermission();
  }

  void DownloadsController::setPermission(bool known, bool allowed) {
    if (known == m_permissionKnown && allowed == m_allowed) return;
    m_permissionKnown = known;
    m_allowed = allowed;
    Q_EMIT permissionChanged();
  }

  void DownloadsController::askPermission() {
    if (m_client == nullptr || !signedIn() || !m_online) return;
    const quint64 attempt = ++m_permissionAttempt;
    m_client->currentUser(this, [this, attempt](api::Result<api::UserInfo> user) {
      if (attempt != m_permissionAttempt || !user) return;
      setPermission(true, user->enableContentDownloading);
    });
  }

  void DownloadsController::refresh() { rebuild(); }

  int DownloadsController::verify() {
    const int marked = m_queue.verifyComplete();
    if (marked > 0) rebuild();
    return marked;
  }

  void DownloadsController::scheduleRebuild() { m_rebuild.start(); }

  bool DownloadsController::queueStopped() const {
    return m_queue.problem() != QueueProblem::None;
  }

  bool DownloadsController::queuePausedByUser() const {
    return m_queue.problem() == QueueProblem::PausedByUser;
  }

  QString DownloadsController::queueProblemText() const { return problemText(m_queue.problem()); }

  QString DownloadsController::problemText(QueueProblem problem) {
    switch (problem) {
    case QueueProblem::None: return {};
    case QueueProblem::PausedByUser: return tr("Downloads are paused.");
    case QueueProblem::SignInRequired: return tr("Sign in again to continue the downloads.");
    case QueueProblem::LowSpace:
      return tr("The disk is almost full. Downloads that do not fit wait until there is free space.");
    case QueueProblem::OverLimit:
      return tr("The space for downloads is used up. Downloads that do not fit wait until you delete "
                "some or raise the limit in Settings.");
    case QueueProblem::Refused: return tr("The server refused the download.");
    case QueueProblem::Network: return tr("The connection to the server failed.");
    case QueueProblem::DiskError: return tr("The download folder cannot be written to.");
    case QueueProblem::NotServerUrl: return tr("The download led away from the server.");
    case QueueProblem::MediaMissing:
      return tr("The downloaded file is missing or damaged. Try again to download it again.");
    }
    return {};
  }

  QString DownloadsController::formatBytes(qint64 bytes) const { return formatBytesText(bytes); }

  QString DownloadsController::formatBytesText(qint64 bytes) {
    const QLocale locale;
    const double value = double(std::max<qint64>(bytes, 0));
    if (bytes >= GiB) return tr("%1 GiB").arg(locale.toString(value / double(GiB), 'f', 1));
    if (bytes >= MiB) return tr("%1 MiB").arg(locale.toString(value / double(MiB), 'f', 0));
    return tr("%1 KiB").arg(locale.toString(std::ceil(value / 1024.0), 'f', 0));
  }

  QString DownloadsController::statusTextFor(const ItemManifest& m, const DownloadStatus* status) const {
    const ItemState state = status != nullptr ? status->state : m.state;
    if (state == ItemState::Complete) {
      QString line = formatBytesText(m.expectedSize);
      if (m.removedOnServer) line += QStringLiteral(" · ") + tr("No longer on the server");
      else if (m.changedOnServer) line += QStringLiteral(" · ") + tr("Changed on the server");
      return line;
    }
    if (state == ItemState::Paused) return tr("Paused");
    if (state == ItemState::Failed)
      return problemText(status != nullptr ? status->problem : QueueProblem::Network);
    if (status != nullptr && status->activity == Activity::RetryWait)
      return tr("Connection lost, trying again");
    if (status != nullptr && status->activity == Activity::Idle && status->problem == QueueProblem::LowSpace)
      return tr("Waiting for free disk space");
    if (status != nullptr && status->activity == Activity::Idle && status->problem == QueueProblem::OverLimit)
      return tr("Waiting: the space for downloads is used up");
    if (status != nullptr && status->activity == Activity::Transferring && status->audioTrack > 0)
      return tr("Audio track %1 of %2").arg(status->audioTrack).arg(status->audioTracks);
    if (status != nullptr && status->activity == Activity::Transferring) {
      if (status->total > 0)
        return tr("%1 of %2").arg(formatBytesText(status->bytesOnDisk), formatBytesText(status->total));
      return formatBytesText(status->bytesOnDisk);
    }
    return tr("Waiting");
  }

  DownloadListModel::Row DownloadsController::rowFor(const ItemManifest& m,
                                                     const DownloadStatus* status) const {
    DownloadListModel::Row row;
    row.itemId = m.itemId;
    row.name = text(m.item, "Name");
    row.type = text(m.item, "Type");
    row.state = status != nullptr ? status->state : m.state;
    row.activity = status != nullptr ? status->activity : Activity::Idle;
    row.bytesOnDisk = status != nullptr ? status->bytesOnDisk
                                        : (m.state == ItemState::Complete ? m.expectedSize : 0);
    row.total = status != nullptr && status->total > 0 ? status->total : m.expectedSize;
    row.statusText = statusTextFor(m, status);
    const QJsonObject userData = userDataOf(m);
    row.played = userData.value(QLatin1String("Played")).toBool();
    row.positionTicks = number(userData, "PlaybackPositionTicks");
    row.runTimeTicks = number(m.item, "RunTimeTicks");
    row.removedOnServer = m.removedOnServer;
    row.changedOnServer = m.changedOnServer;
    row.albumId = text(m.item, "AlbumId");
    if (row.type == QLatin1String("Episode")) {
      row.groupId = text(m.item, "SeriesId");
      row.groupName = text(m.item, "SeriesName");
      QString number;
      if (m.item.contains(QLatin1String("IndexNumber")))
        number = QStringLiteral("S%1:E%2")
                     .arg(m.item.value(QLatin1String("ParentIndexNumber")).toInt())
                     .arg(m.item.value(QLatin1String("IndexNumber")).toInt());
      QStringList parts{row.groupName, number};
      parts.removeAll(QString());
      row.subtitle = parts.join(QStringLiteral(" · "));
    } else if (row.type == QLatin1String("Audio")) {
      row.groupId = row.albumId;
      row.groupName = text(m.item, "Album");
      QString artist = text(m.item, "AlbumArtist");
      if (artist.isEmpty()) {
        QStringList artists;
        for (const QJsonValue& a : m.item.value(QLatin1String("Artists")).toArray())
          artists.append(a.toString());
        artist = artists.join(QStringLiteral(", "));
      }
      QStringList parts{artist, row.groupName};
      parts.removeAll(QString());
      row.subtitle = parts.join(QStringLiteral(" · "));
    } else if (m.item.contains(QLatin1String("ProductionYear"))) {
      row.subtitle = QString::number(m.item.value(QLatin1String("ProductionYear")).toInt());
    }
    row.imageUrl = imageUrlOf(m);
    return row;
  }

  QUrl DownloadsController::imageUrlOf(const ItemManifest& m) const {
    for (const char* type : {"Primary", "Thumb"}) {
      const QString file = m.imageFiles.value(QLatin1String(type));
      if (file.isEmpty()) continue;
      const QString path = m_store.itemDir(m) + QLatin1Char('/') + file;
      if (QFileInfo::exists(path)) return QUrl::fromLocalFile(path);
    }
    return {};
  }

  void DownloadsController::rebuild() {
    m_rebuild.stop();
    m_members.clear();
    m_groups.clear();
    QList<DownloadListModel::Row> queueRows;
    QList<DownloadListModel::Row> doneRows;
    qint64 used = 0;
    qint64 watched = 0;
    if (signedIn()) {
      QList<ItemManifest> items = m_catalog.items(m_serverId, m_userId);
      for (const ItemManifest& m : std::as_const(items)) {
        m_members.insert(m.itemId, m);
        for (const QString& group : groupIdsOf(m)) m_groups[group].append(m.itemId);
      }
      for (const QString& id : m_queue.order()) {
        const auto member = m_members.constFind(id);
        if (member == m_members.cend()) continue;
        const auto status = queuedStatus(id);
        queueRows.append(rowFor(*member, status ? &*status : nullptr));
        if (status) used += status->bytesOnDisk;
      }
      QList<ItemManifest> complete;
      for (const ItemManifest& m : std::as_const(items))
        if (m.state == ItemState::Complete && !queuedStatus(m.itemId)) complete.append(m);
      std::sort(complete.begin(), complete.end(),
                [](const ItemManifest& a, const ItemManifest& b) { return sortKey(a) < sortKey(b); });
      for (const ItemManifest& m : std::as_const(complete)) {
        doneRows.append(rowFor(m, nullptr));
        used += m.expectedSize;
        if (userDataOf(m).value(QLatin1String("Played")).toBool()) watched += m.expectedSize;
      }
    }
    m_queueModel.setRows(std::move(queueRows));
    m_downloadedModel.setRows(std::move(doneRows));
    m_usedBytes = used;
    m_watchedBytes = watched;
    updateStorage();
    ++m_revision;
    Q_EMIT revisionChanged();
  }

  void DownloadsController::updateStorage() {
    const auto free = m_guard.freeBytes(existingFolder(m_store.root()));
    m_freeBytes = free ? *free : -1;
    Q_EMIT storageChanged();
  }

  QList<ItemManifest> DownloadsController::completeItems() const {
    QList<ItemManifest> out;
    for (const ItemManifest& m : m_members)
      if (m.state == ItemState::Complete && !queuedStatus(m.itemId)) out.append(m);
    std::sort(out.begin(), out.end(),
              [](const ItemManifest& a, const ItemManifest& b) { return sortKey(a) < sortKey(b); });
    return out;
  }

  QStringList DownloadsController::membersOf(const QString& id) const {
    if (m_members.contains(id)) return {id};
    return m_groups.value(id);
  }

  QString DownloadsController::stateOf(const QString& itemId) const {
    const QStringList members = membersOf(itemId);
    if (members.isEmpty()) return {};
    bool downloading = false, queued = false, failed = false, paused = false;
    for (const QString& id : members) {
      const auto status = queuedStatus(id);
      if (!status) continue;
      if (status->activity != Activity::Idle) downloading = true;
      else if (status->state == ItemState::Failed) failed = true;
      else if (status->state == ItemState::Paused) paused = true;
      else if (status->state != ItemState::Complete) queued = true;
    }
    if (downloading) return QStringLiteral("downloading");
    if (queued) return QStringLiteral("queued");
    if (failed) return QStringLiteral("failed");
    if (paused) return QStringLiteral("paused");
    return QStringLiteral("complete");
  }

  double DownloadsController::progressOf(const QString& itemId) const {
    std::int64_t done = 0;
    std::int64_t total = 0;
    for (const QString& id : membersOf(itemId)) {
      const ItemManifest& m = m_members[id];
      if (const auto status = queuedStatus(id)) {
        done += status->bytesOnDisk;
        total += status->total > 0 ? status->total : m.expectedSize;
      } else {
        done += m.expectedSize;
        total += m.expectedSize;
      }
    }
    return total > 0 ? std::min(1.0, double(done) / double(total)) : 0.0;
  }

  QVariantMap DownloadsController::groupSummary(const QString& parentId) const {
    int complete = 0;
    const QStringList members = membersOf(parentId);
    for (const QString& id : members)
      if (!queuedStatus(id) && m_members[id].state == ItemState::Complete) ++complete;
    return {{QStringLiteral("count"), int(members.size())},
            {QStringLiteral("complete"), complete},
            {QStringLiteral("bytes"), bytesOf(parentId)}};
  }

  qint64 DownloadsController::bytesOf(const QString& itemId) const {
    qint64 bytes = 0;
    for (const QString& id : membersOf(itemId)) {
      if (const auto status = queuedStatus(id)) bytes += status->bytesOnDisk;
      else bytes += m_members[id].expectedSize;
    }
    return bytes;
  }

  // --- Download ------------------------------------------------------------

  int DownloadsController::prepare(const QString& itemId, bool unplayedOnly) {
    const int requestId = m_nextRequest++;
    const auto fail = [this, requestId](const QString& why) {
      QTimer::singleShot(0, this, [this, requestId, why] { Q_EMIT prepareFailed(requestId, why); });
    };
    if (m_client == nullptr || !signedIn()) {
      fail(tr("Sign in to download."));
      return requestId;
    }
    if (!allowed()) {
      fail(tr("Your account on this server may not download files."));
      return requestId;
    }
    api::fetchItemsById(
        *m_client, {itemId}, this,
        [this, requestId, itemId, unplayedOnly](api::Result<QJsonArray> found) {
          if (!found) {
            Q_EMIT prepareFailed(requestId, tr("The server did not answer: %1").arg(found.error().message));
            return;
          }
          if (found->isEmpty()) {
            Q_EMIT prepareFailed(requestId, tr("The server no longer has this item."));
            return;
          }
          const QJsonObject item = found->first().toObject();
          const QString title = text(item, "Name");
          const QString type = text(item, "Type");
          if (playable(item)) {
            prepareItems(requestId, QString(), title, type, QJsonArray{item});
            return;
          }
          if (m_client == nullptr) return;
          const bool keepOrder = type == QLatin1String("Playlist");
          api::fetchDownloadableChildren(
              *m_client, itemId, keepOrder, unplayedOnly, this,
              [this, requestId, itemId, title, type](api::Result<QJsonArray> children) {
                if (!children) {
                  Q_EMIT prepareFailed(requestId,
                                       tr("The server did not answer: %1").arg(children.error().message));
                  return;
                }
                prepareItems(requestId, itemId, title, type, *children);
              });
        });
    return requestId;
  }

  void DownloadsController::prepareItems(int requestId, const QString& parentId, const QString& title,
                                         const QString& type, const QJsonArray& items) {
    Plan plan;
    plan.parentId = parentId;
    int fresh = 0, already = 0, unavailable = 0, compressed = 0;
    QString presetLabel;
    qint64 bytes = 0;
    QVariantList versions;
    for (const QJsonValue& value : items) {
      Candidate candidate;
      candidate.item = value.toObject();
      const bool audio = text(candidate.item, "MediaType") == QLatin1String("Audio");
      const auto preset = presetForQuality(m_quality, audio);
      if (preset) candidate.preset = preset->id;
      for (const QJsonValue& source : candidate.item.value(QLatin1String("MediaSources")).toArray())
        if (preset || api::originalAvailable(candidate.item, source.toObject()))
          candidate.sources.append(source);
      const QString id = text(candidate.item, "Id");
      if (m_members.contains(id)) ++already;
      else if (candidate.sources.isEmpty()) ++unavailable;
      else {
        ++fresh;
        if (preset) {
          ++compressed;
          const QJsonObject source = candidate.sources.first().toObject();
          std::int64_t ticks = number(source, "RunTimeTicks");
          if (ticks <= 0) ticks = number(candidate.item, "RunTimeTicks");
          bytes += api::estimatedCompressedSize(preset->profile, ticks);
          if (presetLabel.isEmpty()) presetLabel = QCoreApplication::translate("DownloadSettings", preset->label);
        } else {
          bytes += number(candidate.sources.first().toObject(), "Size");
        }
      }
      plan.items.append(candidate);
    }
    const bool single = parentId.isEmpty() && plan.items.size() == 1;
    if (single && fresh == 1 && plan.items.first().sources.size() > 1) {
      for (const QJsonValue& source : plan.items.first().sources) {
        const QJsonObject s = source.toObject();
        QString name = text(s, "Name");
        if (name.isEmpty()) name = text(s, "Container").toUpper();
        versions.append(QVariantMap{{QStringLiteral("name"), name},
                                    {QStringLiteral("bytes"), qint64(number(s, "Size"))}});
      }
    }

    bool fits = true;
    QString spaceText;
    const std::int64_t need = StorageGuard::withMargin(bytes);
    const std::int64_t limit = m_guard.limitBytes();
    const std::int64_t committed = m_catalog.estimatedBytes();
    if (fresh > 0 && limit > 0 && committed + need > limit) {
      fits = false;
      spaceText = tr("This needs %1, but the space for downloads has %2 left.")
                      .arg(formatBytesText(need), formatBytesText(std::max<std::int64_t>(0, limit - committed)));
    }
    const auto free = m_guard.freeBytes(existingFolder(m_store.root()));
    if (fits && fresh > 0 && (!free || *free - need < m_guard.reserveBytes())) {
      fits = false;
      spaceText = !free ? tr("The download folder's disk is not available.")
                        : tr("This needs %1, but the disk has %2 free and %3 are kept free.")
                              .arg(formatBytesText(need), formatBytesText(*free),
                                   formatBytesText(m_guard.reserveBytes()));
    }

    m_plans.insert(requestId, plan);
    Q_EMIT prepared(requestId, QVariantMap{
                                   {QStringLiteral("title"), title},
                                   {QStringLiteral("type"), type},
                                   {QStringLiteral("count"), fresh},
                                   {QStringLiteral("alreadyCount"), already},
                                   {QStringLiteral("unavailableCount"), unavailable},
                                   {QStringLiteral("bytes"), bytes},
                                   {QStringLiteral("single"), single},
                                   {QStringLiteral("versions"), versions},
                                   {QStringLiteral("fits"), fits},
                                   {QStringLiteral("spaceText"), spaceText},
                                   // Empty for originals (the dialog says so).
                                   {QStringLiteral("qualityText"),
                                    compressed == 0 ? QString()
                                                    : compressed > 10
                                                          ? tr("Quality: compressed by the server (%1), "
                                                               "one item at a time. So many items keep "
                                                               "the server busy for a long time.")
                                                                .arg(presetLabel)
                                                          : tr("Quality: compressed by the server (%1), "
                                                               "smaller, one audio track.")
                                                                .arg(presetLabel)},
                               });
  }

  int DownloadsController::confirm(int requestId, int version) {
    const auto found = m_plans.constFind(requestId);
    if (found == m_plans.cend()) return 0;
    const Plan plan = *found;
    m_plans.erase(found);
    const bool single = plan.parentId.isEmpty() && plan.items.size() == 1;
    const QStringList parents = plan.parentId.isEmpty() ? QStringList() : QStringList{plan.parentId};
    int queued = 0;
    bool failedToWrite = false;
    for (const Candidate& candidate : plan.items) {
      const QString id = text(candidate.item, "Id");
      if (const auto existing = m_catalog.find(m_serverId, m_userId, id)) {
        // Already downloaded: it now also belongs to this group.
        if (!plan.parentId.isEmpty() && existing->state == ItemState::Complete &&
            !existing->parents.contains(plan.parentId)) {
          ItemManifest m = *existing;
          m.parents.append(plan.parentId);
          if (m_store.writeManifest(m)) m_catalog.update(m);
        }
        continue;
      }
      if (candidate.sources.isEmpty()) continue;
      const int index = single ? std::clamp(version, 0, int(candidate.sources.size()) - 1) : 0;
      const QJsonObject userData = candidate.item.value(QLatin1String("UserData")).toObject();
      const EnqueueResult result =
          candidate.preset.isEmpty()
              ? m_queue.enqueueOriginal(candidate.item, candidate.sources.at(index).toObject(), userData,
                                        parents)
              : m_queue.enqueueCompressed(candidate.item, candidate.sources.at(index).toObject(),
                                          candidate.preset, std::nullopt, userData, parents);
      if (result == EnqueueResult::Queued) ++queued;
      else if (result == EnqueueResult::DiskError) failedToWrite = true;
    }
    if (failedToWrite) Q_EMIT message(problemText(QueueProblem::DiskError), true);
    rebuild();
    return queued;
  }

  void DownloadsController::discard(int requestId) { m_plans.remove(requestId); }

  // --- The queue -------------------------------------------------------------

  void DownloadsController::pause(const QString& itemId) { m_queue.pause(itemId); }
  void DownloadsController::resume(const QString& itemId) { m_queue.resume(itemId); }
  void DownloadsController::cancel(const QString& itemId) { m_queue.cancel(itemId); }
  void DownloadsController::retry(const QString& itemId) { m_queue.retry(itemId); }
  void DownloadsController::pauseAll() { m_queue.pauseAll(); }
  void DownloadsController::resumeAll() { m_queue.resumeAll(); }

  void DownloadsController::moveUp(const QString& itemId) {
    const qsizetype at = m_queue.order().indexOf(itemId);
    if (at > 0) m_queue.move(itemId, int(at - 1));
  }

  void DownloadsController::moveDown(const QString& itemId) {
    const QStringList order = m_queue.order();
    const qsizetype at = order.indexOf(itemId);
    if (at >= 0 && at + 1 < order.size()) m_queue.move(itemId, int(at + 1));
  }

  // --- Deleting ------------------------------------------------------------

  bool DownloadsController::removeOne(const QString& itemId) {
    if (queuedStatus(itemId)) {
      m_queue.cancel(itemId);
      return true;
    }
    const auto member = m_members.constFind(itemId);
    if (member == m_members.cend()) return true;
    const ItemManifest manifest = *member;
    if (!m_store.removeItem(manifest)) {
      Q_EMIT message(tr("\"%1\" could not be deleted. If it is playing, stop it and try again.")
                         .arg(text(manifest.item, "Name")),
                     true);
      return false;
    }
    m_catalog.remove(manifest.serverId, manifest.userId, manifest.itemId);
    return true;
  }

  bool DownloadsController::remove(const QString& itemId) {
    bool ok = true;
    for (const QString& id : membersOf(itemId)) ok = removeOne(id) && ok;
    // Space freed: items waiting for it may go on.
    m_queue.recheckSpace();
    rebuild();
    return ok;
  }

  int DownloadsController::removeWatched() {
    int removed = 0;
    for (const ItemManifest& m : completeItems())
      if (userDataOf(m).value(QLatin1String("Played")).toBool() && removeOne(m.itemId)) ++removed;
    m_queue.recheckSpace();
    rebuild();
    return removed;
  }

  int DownloadsController::removeAll() {
    int removed = 0;
    const QStringList ids = m_members.keys();
    for (const QString& id : ids)
      if (removeOne(id)) ++removed;
    m_queue.recheckSpace();
    rebuild();
    return removed;
  }

} // namespace jfd::offline
