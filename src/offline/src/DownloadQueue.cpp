#include <jfd/offline/DownloadQueue.h>

#include <jfd/api/Downloads.h>

#include <QDateTime>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace jfd::offline {

  namespace {
    Q_LOGGING_CATEGORY(lcQueue, "jfd.offline.queue")
  } // namespace

  DownloadQueue::DownloadQueue(DownloadStore& store, OfflineCatalog& catalog,
                               std::shared_ptr<StorageGuard> guard, QObject* parent)
      : QObject(parent), m_store(store), m_catalog(catalog), m_guard(std::move(guard)) {
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("jfd-downloads"));
    auto* worker = new DownloadWorker(m_guard);
    worker->moveToThread(m_thread);
    m_worker = worker;
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &DownloadWorker::transferStarted, this, &DownloadQueue::onStarted);
    connect(worker, &DownloadWorker::transferProgress, this, &DownloadQueue::onProgress);
    connect(worker, &DownloadWorker::transferFinished, this, &DownloadQueue::onFinished);
    m_thread->start();
  }

  DownloadQueue::~DownloadQueue() {
    if (m_worker)
      QMetaObject::invokeMethod(m_worker, &DownloadWorker::stopAll, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
  }

  void DownloadQueue::setCheckEveryBytes(std::int64_t bytes) {
    QPointer<DownloadWorker> worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, bytes]() {
      if (worker) worker->setCheckEveryBytes(bytes);
    });
  }

  void DownloadQueue::setStallTimeoutMs(int ms) {
    QPointer<DownloadWorker> worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, ms]() {
      if (worker) worker->setStallTimeoutMs(ms);
    });
  }

  void DownloadQueue::setAccess(const QString& serverId, const QString& userId,
                                const QUrl& serverUrl, const QByteArray& authorization) {
    const bool sameUser = serverId == m_serverId && userId == m_userId;
    m_serverUrl = serverUrl;
    m_authorization = authorization;
    if (!sameUser || serverUrl.isEmpty()) {
      stopAllTransfers();
      m_serverId = serverId;
      m_userId = userId;
      reload();
    }
    if (m_problem == QueueProblem::SignInRequired && !serverUrl.isEmpty())
      setProblem(QueueProblem::None);
    schedule();
  }

  void DownloadQueue::setMaxParallel(int count) {
    m_maxParallel = std::clamp(count, 1, 3);
    schedule();
  }

  void DownloadQueue::reload() {
    for (Entry& entry : m_entries)
      delete entry.retryTimer;
    m_entries.clear();
    m_order.clear();
    if (m_serverId.isEmpty() || m_userId.isEmpty()) {
      Q_EMIT orderChanged();
      return;
    }
    QList<ItemManifest> pending;
    for (ItemManifest& m : m_catalog.items(m_serverId, m_userId)) {
      if (m.state == ItemState::Complete) continue;
      // Killed between the rename and the manifest: the file is there and
      // has the expected size, so it is complete.
      const QString media = m_store.mediaPath(m);
      if (m.expectedSize > 0 && !media.isEmpty() && QFileInfo(media).size() == m.expectedSize &&
          !QFileInfo::exists(m_store.partPath(m))) {
        m.state = ItemState::Complete;
        m.completedAt = QDateTime::currentDateTimeUtc();
        m.resumeValidator.clear();
        if (m_store.writeManifest(m)) m_catalog.update(m);
        continue;
      }
      pending.append(m);
    }
    std::stable_sort(
        pending.begin(), pending.end(),
        [](const ItemManifest& a, const ItemManifest& b) { return a.queuedAt < b.queuedAt; });
    for (const ItemManifest& m : pending) {
      Entry entry;
      entry.manifest = m;
      entry.bytesOnDisk = QFileInfo(m_store.partPath(m)).size();
      entry.total = m.expectedSize;
      m_order.append(m.itemId);
      m_entries.insert(m.itemId, std::move(entry));
    }
    Q_EMIT orderChanged();
  }

  EnqueueResult DownloadQueue::enqueueOriginal(const QJsonObject& item,
                                               const QJsonObject& mediaSource,
                                               const QJsonObject& userData) {
    if (m_serverId.isEmpty() || m_userId.isEmpty()) return EnqueueResult::NotSignedIn;
    const QString itemId = item.value(QLatin1String("Id")).toString();
    const QString sourceId = mediaSource.value(QLatin1String("Id")).toString();
    if (!DownloadStore::isSafeId(itemId) || !DownloadStore::isSafeId(sourceId))
      return EnqueueResult::InvalidItem;
    if (!api::originalAvailable(item, mediaSource)) return EnqueueResult::OriginalUnavailable;
    if (m_catalog.find(m_serverId, m_userId, itemId)) return EnqueueResult::AlreadyThere;

    ItemManifest m;
    m.serverId = m_serverId;
    m.userId = m_userId;
    m.itemId = itemId;
    m.state = ItemState::Queued;
    m.quality = QStringLiteral("original");
    m.item = item;
    m.mediaSource = mediaSource;
    m.userData = userData;
    m.mediaFile = QStringLiteral("media.") + mediaExtension(mediaSource);
    const double size = mediaSource.value(QLatin1String("Size")).toDouble();
    m.expectedSize = size > 0 ? static_cast<std::int64_t>(size) : 0;
    m.queuedAt = QDateTime::currentDateTimeUtc();
    if (!m_order.isEmpty()) {
      const QDateTime last = m_entries.value(m_order.last()).manifest.queuedAt;
      if (last.isValid() && m.queuedAt <= last) m.queuedAt = last.addMSecs(1);
    }
    if (!m_store.writeManifest(m)) return EnqueueResult::DiskError;
    // What was stored, secrets removed, is what the queue keeps.
    if (const auto stored = m_store.readManifest(m.serverId, m.userId, m.itemId)) m = *stored;
    m_catalog.update(m);

    Entry entry;
    entry.manifest = m;
    entry.total = m.expectedSize;
    m_order.append(itemId);
    m_entries.insert(itemId, std::move(entry));
    Q_EMIT orderChanged();
    Q_EMIT itemChanged(itemId);
    schedule();
    return EnqueueResult::Queued;
  }

  int DownloadQueue::activeCount() const {
    int active = 0;
    for (const Entry& entry : m_entries)
      // An item between retries keeps its slot.
      if (entry.activity != Activity::Idle) ++active;
    return active;
  }

  std::int64_t DownloadQueue::committedBytes(const Entry& next) const {
    // Complete downloads of every user, the ones in progress and `next`:
    // items further down the queue are not counted against the limit yet.
    std::int64_t total = 0;
    for (const ItemManifest& m : m_catalog.all()) {
      if (m.state == ItemState::Complete) {
        total += m.expectedSize;
        continue;
      }
      if (m.serverId != m_serverId || m.userId != m_userId) continue;
      const auto it = m_entries.constFind(m.itemId);
      if (it != m_entries.constEnd() && (it->activity != Activity::Idle || it->bytesOnDisk > 0))
        total += m.expectedSize;
    }
    if (next.activity == Activity::Idle && next.bytesOnDisk == 0)
      total += next.manifest.expectedSize;
    return total;
  }

  void DownloadQueue::schedule() {
    if (m_problem != QueueProblem::None || m_serverUrl.isEmpty() || !m_worker) return;
    int active = activeCount();
    for (const QString& itemId : std::as_const(m_order)) {
      if (active >= m_maxParallel) break;
      Entry& entry = m_entries[itemId];
      if (entry.manifest.state != ItemState::Queued || entry.activity != Activity::Idle) continue;
      const std::int64_t remaining =
          std::max<std::int64_t>(0, entry.manifest.expectedSize - entry.bytesOnDisk);
      const StorageVerdict verdict =
          m_guard ? m_guard->admit(m_store.root(), remaining, committedBytes(entry))
                  : StorageVerdict::Ok;
      if (verdict == StorageVerdict::OverLimit) {
        setProblem(QueueProblem::OverLimit);
        return;
      }
      if (verdict == StorageVerdict::LowSpace) {
        setProblem(QueueProblem::LowSpace);
        return;
      }
      startTransfer(entry);
      ++active;
    }
  }

  void DownloadQueue::startTransfer(Entry& entry) {
    const ItemManifest& m = entry.manifest;
    DownloadJob job;
    job.key = m.serverId + QLatin1Char('/') + m.userId + QLatin1Char('/') + m.itemId;
    job.url = api::originalDownloadUrl(m_serverUrl,
                                       m.mediaSource.value(QLatin1String("Id")).toString(m.itemId));
    job.serverUrl = m_serverUrl;
    job.authorization = m_authorization;
    job.partPath = m_store.partPath(m);
    job.finalPath = m_store.mediaPath(m);
    job.expectedSize = m.expectedSize;
    job.resumeValidator = m.resumeValidator;
    entry.activity = Activity::Transferring;
    entry.afterStop = Entry::AfterStop::Requeue;
    QPointer<DownloadWorker> worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, job]() {
      if (worker) worker->start(job);
    });
    Q_EMIT itemChanged(m.itemId);
  }

  void DownloadQueue::stopTransfer(Entry& entry, Entry::AfterStop after) {
    entry.afterStop = after;
    const QString key = entry.manifest.serverId + QLatin1Char('/') + entry.manifest.userId +
                        QLatin1Char('/') + entry.manifest.itemId;
    QPointer<DownloadWorker> worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, key]() {
      if (worker) worker->stop(key);
    });
  }

  void DownloadQueue::stopAllTransfers() {
    for (Entry& entry : m_entries) {
      if (entry.retryTimer) {
        entry.retryTimer->stop();
        if (entry.activity == Activity::RetryWait) entry.activity = Activity::Idle;
      }
      if (entry.activity == Activity::Transferring) stopTransfer(entry, Entry::AfterStop::Requeue);
    }
  }

  void DownloadQueue::setProblem(QueueProblem problem) {
    if (m_problem == problem) return;
    m_problem = problem;
    if (problem != QueueProblem::None) {
      qCInfo(lcQueue) << "queue stopped:" << static_cast<int>(problem);
      stopAllTransfers();
    }
    Q_EMIT problemChanged(problem);
  }

  void DownloadQueue::save(Entry& entry) {
    if (!m_store.writeManifest(entry.manifest))
      qCWarning(lcQueue) << "could not write the manifest of" << entry.manifest.itemId;
    m_catalog.update(entry.manifest);
  }

  void DownloadQueue::dropEntry(const QString& itemId) {
    const auto it = m_entries.find(itemId);
    if (it == m_entries.end()) return;
    delete it->retryTimer;
    m_entries.erase(it);
    m_order.removeAll(itemId);
    Q_EMIT orderChanged();
  }

  void DownloadQueue::pause(const QString& itemId) {
    const auto it = m_entries.find(itemId);
    if (it == m_entries.end() || it->manifest.state != ItemState::Queued) return;
    if (it->activity == Activity::Transferring) {
      stopTransfer(*it, Entry::AfterStop::Pause);
      return;
    }
    if (it->retryTimer) it->retryTimer->stop();
    it->activity = Activity::Idle;
    it->manifest.state = ItemState::Paused;
    save(*it);
    Q_EMIT itemChanged(itemId);
  }

  void DownloadQueue::resume(const QString& itemId) {
    const auto it = m_entries.find(itemId);
    if (it == m_entries.end() || it->manifest.state != ItemState::Paused) return;
    it->manifest.state = ItemState::Queued;
    save(*it);
    Q_EMIT itemChanged(itemId);
    schedule();
  }

  void DownloadQueue::retry(const QString& itemId) {
    const auto it = m_entries.find(itemId);
    if (it == m_entries.end() || it->manifest.state != ItemState::Failed) return;
    it->manifest.state = ItemState::Queued;
    it->attempts = 0;
    it->problem = QueueProblem::None;
    save(*it);
    Q_EMIT itemChanged(itemId);
    schedule();
  }

  void DownloadQueue::cancel(const QString& itemId) {
    const auto it = m_entries.find(itemId);
    if (it == m_entries.end()) return;
    if (it->activity == Activity::Transferring) {
      stopTransfer(*it, Entry::AfterStop::Remove);
      return;
    }
    const ItemManifest manifest = it->manifest;
    dropEntry(itemId);
    m_store.removeItem(manifest);
    m_catalog.remove(manifest.serverId, manifest.userId, manifest.itemId);
    Q_EMIT itemChanged(itemId);
    schedule();
  }

  void DownloadQueue::move(const QString& itemId, int index) {
    const qsizetype from = m_order.indexOf(itemId);
    if (from < 0) return;
    const qsizetype to = std::clamp<qsizetype>(index, 0, m_order.size() - 1);
    if (from == to) return;
    m_order.move(from, to);
    renumber();
    Q_EMIT orderChanged();
    // The new first item does not overtake one already running.
    schedule();
  }

  void DownloadQueue::renumber() {
    // The manifests keep the order across restarts: renumber queuedAt.
    QDateTime when;
    for (const QString& id : std::as_const(m_order)) {
      const QDateTime at = m_entries.value(id).manifest.queuedAt;
      if (at.isValid() && (!when.isValid() || at < when)) when = at;
    }
    if (!when.isValid()) when = QDateTime::currentDateTimeUtc();
    for (const QString& id : std::as_const(m_order)) {
      Entry& entry = m_entries[id];
      if (entry.manifest.queuedAt != when) {
        entry.manifest.queuedAt = when;
        save(entry);
      }
      when = when.addMSecs(1);
    }
  }

  void DownloadQueue::pauseAll() {
    setProblem(QueueProblem::PausedByUser);
  }

  void DownloadQueue::resumeAll() {
    if (m_problem == QueueProblem::SignInRequired) return;
    setProblem(QueueProblem::None);
    schedule();
  }

  std::optional<DownloadStatus> DownloadQueue::status(const QString& itemId) const {
    const auto it = m_entries.constFind(itemId);
    if (it != m_entries.constEnd()) {
      DownloadStatus s;
      s.state = it->manifest.state;
      s.activity = it->activity;
      s.bytesOnDisk = it->bytesOnDisk;
      s.total = it->total;
      s.problem = it->problem;
      return s;
    }
    if (const auto m = m_catalog.find(m_serverId, m_userId, itemId)) {
      DownloadStatus s;
      s.state = m->state;
      s.bytesOnDisk = m->expectedSize;
      s.total = m->expectedSize;
      return s;
    }
    return std::nullopt;
  }

  namespace {
    // "server/user/item" -> the item id, when it is the given user's.
    QString itemOf(const QString& key, const QString& serverId, const QString& userId) {
      const QString prefix = serverId + QLatin1Char('/') + userId + QLatin1Char('/');
      return key.startsWith(prefix) ? key.mid(prefix.size()) : QString();
    }
  } // namespace

  void DownloadQueue::onStarted(const QString& key, qint64 offset, qint64 total,
                                const QString& validator) {
    const QString itemId = itemOf(key, m_serverId, m_userId);
    const auto it = m_entries.find(itemId);
    if (itemId.isEmpty() || it == m_entries.end()) return;
    Entry& entry = *it;
    bool changed = false;
    if (total > 0 && entry.manifest.expectedSize != total) {
      entry.manifest.expectedSize = total;
      changed = true;
    }
    if (entry.manifest.resumeValidator != validator) {
      entry.manifest.resumeValidator = validator;
      changed = true;
    }
    if (changed) save(entry);
    entry.bytesOnDisk = offset;
    entry.total = total > 0 ? total : entry.manifest.expectedSize;
    Q_EMIT itemChanged(itemId);
  }

  void DownloadQueue::onProgress(const QString& key, qint64 bytesOnDisk, qint64 total) {
    const QString itemId = itemOf(key, m_serverId, m_userId);
    const auto it = m_entries.find(itemId);
    if (itemId.isEmpty() || it == m_entries.end()) return;
    it->bytesOnDisk = bytesOnDisk;
    if (total > 0) it->total = total;
    Q_EMIT progress(itemId, bytesOnDisk, it->total);
  }

  void DownloadQueue::onFinished(const QString& key, int outcomeValue, qint64 received) {
    const QString itemId = itemOf(key, m_serverId, m_userId);
    const auto it = m_entries.find(itemId);
    if (itemId.isEmpty() || it == m_entries.end()) return;
    Entry& entry = *it;
    entry.activity = Activity::Idle;
    const auto outcome = static_cast<TransferOutcome>(outcomeValue);
    entry.bytesOnDisk = QFileInfo(m_store.partPath(entry.manifest)).size();

    const auto fail = [&](QueueProblem problem) {
      entry.manifest.state = ItemState::Failed;
      entry.problem = problem;
      save(entry);
      Q_EMIT itemChanged(itemId);
      Q_EMIT failed(itemId, problem);
    };

    switch (outcome) {
    case TransferOutcome::Complete: {
      ItemManifest done = entry.manifest;
      done.state = ItemState::Complete;
      done.completedAt = QDateTime::currentDateTimeUtc();
      done.resumeValidator.clear();
      const qint64 size = QFileInfo(m_store.mediaPath(done)).size();
      if (size > 0) done.expectedSize = size;
      entry.manifest = done;
      save(entry);
      dropEntry(itemId);
      Q_EMIT itemChanged(itemId);
      Q_EMIT completed(itemId);
      break;
    }
    case TransferOutcome::Cancelled:
      if (entry.afterStop == Entry::AfterStop::Remove) {
        const ItemManifest manifest = entry.manifest;
        dropEntry(itemId);
        m_store.removeItem(manifest);
        m_catalog.remove(manifest.serverId, manifest.userId, manifest.itemId);
      } else if (entry.afterStop == Entry::AfterStop::Pause) {
        entry.manifest.state = ItemState::Paused;
        save(entry);
      }
      Q_EMIT itemChanged(itemId);
      break;
    case TransferOutcome::AuthRequired:
      setProblem(QueueProblem::SignInRequired);
      Q_EMIT itemChanged(itemId);
      return;
    case TransferOutcome::LowSpace:
      setProblem(QueueProblem::LowSpace);
      Q_EMIT itemChanged(itemId);
      return;
    case TransferOutcome::Refused:
      fail(QueueProblem::Refused);
      break;
    case TransferOutcome::NotServerUrl:
      fail(QueueProblem::NotServerUrl);
      break;
    case TransferOutcome::DiskError:
      fail(QueueProblem::DiskError);
      break;
    case TransferOutcome::SizeMismatch:
    case TransferOutcome::NetworkError: {
      if (outcome == TransferOutcome::SizeMismatch) {
        // Start the file over.
        entry.manifest.resumeValidator.clear();
        save(entry);
      }
      if (received > 0) entry.attempts = 0;
      if (entry.attempts >= m_retryDelays.size()) {
        fail(QueueProblem::Network);
        break;
      }
      const int delay = m_retryDelays.at(entry.attempts++);
      entry.activity = Activity::RetryWait;
      if (!entry.retryTimer) {
        entry.retryTimer = new QTimer(this);
        entry.retryTimer->setSingleShot(true);
        connect(entry.retryTimer, &QTimer::timeout, this, [this, itemId]() {
          const auto again = m_entries.find(itemId);
          if (again == m_entries.end() || again->activity != Activity::RetryWait) return;
          again->activity = Activity::Idle;
          schedule();
        });
      }
      entry.retryTimer->start(delay);
      Q_EMIT itemChanged(itemId);
      break;
    }
    }
    schedule();
  }

} // namespace jfd::offline
