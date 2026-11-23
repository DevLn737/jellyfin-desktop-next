#include <jfd/offline/DownloadQueue.h>

#include <jfd/api/Downloads.h>
#include <jfd/offline/CompressedDownload.h>
#include <jfd/offline/LibraryNaming.h>

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QUuid>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace jfd::offline {

  namespace {
    Q_LOGGING_CATEGORY(lcQueue, "jfd.offline.queue")

    constexpr int SpaceRecheckMs = 30'000;

    // Problems that stop the whole queue; the space problems only hold
    // back the items that do not fit.
    bool stopsQueue(QueueProblem problem) {
      return problem == QueueProblem::PausedByUser || problem == QueueProblem::SignInRequired;
    }

    bool isSpaceProblem(QueueProblem problem) {
      return problem == QueueProblem::LowSpace || problem == QueueProblem::OverLimit;
    }

    // The external audio tracks of an original still to fetch, in index
    // order. Compressed copies carry their one audio track inside.
    QList<QJsonObject> pendingAudio(const ItemManifest& m) {
      QList<QJsonObject> out;
      if (m.format == 1 || m.quality != QLatin1String("original")) return out;
      for (const QJsonValue& value : m.mediaSource.value(QLatin1String("MediaStreams")).toArray()) {
        const QJsonObject stream = value.toObject();
        if (stream.value(QLatin1String("Type")).toString() != QLatin1String("Audio") ||
            !stream.value(QLatin1String("IsExternal")).toBool())
          continue;
        const int index = stream.value(QLatin1String("Index")).toInt(-1);
        if (index < 0 || m.audioFiles.contains(index) || m.audioSkipped.contains(index)) continue;
        out.append(stream);
      }
      return out;
    }

    int externalAudioCount(const ItemManifest& m) {
      return int(pendingAudio(m).size() + m.audioFiles.size() + m.audioSkipped.size());
    }

    // Bytes an external audio track should take: its bit rate over the run
    // time (1 MiB when unknown).
    std::int64_t audioEstimate(const ItemManifest& m, const QJsonObject& stream) {
      double ticks = m.mediaSource.value(QLatin1String("RunTimeTicks")).toDouble();
      if (ticks <= 0) ticks = m.item.value(QLatin1String("RunTimeTicks")).toDouble();
      const double bitRate = stream.value(QLatin1String("BitRate")).toDouble();
      if (ticks <= 0 || bitRate <= 0) return 1024 * 1024;
      return static_cast<std::int64_t>(bitRate / 8.0 * ticks / 1e7);
    }

    std::int64_t pendingAudioBytes(const ItemManifest& m) {
      std::int64_t total = 0;
      for (const QJsonObject& stream : pendingAudio(m)) total += audioEstimate(m, stream);
      return total;
    }
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
    connect(worker, &DownloadWorker::transcodeStarted, this, &DownloadQueue::onTranscodeStarted);
    connect(worker, &DownloadWorker::segmentWritten, this, &DownloadQueue::onSegmentWritten);
    m_thread->start();
    // Space freed outside the app (or a drive connected again) lets the
    // waiting items go on without a press of "Continue".
    m_spaceTimer = new QTimer(this);
    m_spaceTimer->setInterval(SpaceRecheckMs);
    connect(m_spaceTimer, &QTimer::timeout, this, &DownloadQueue::schedule);
  }

  void DownloadQueue::setSpaceRecheckMs(int ms) { m_spaceTimer->setInterval(ms); }

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

  void DownloadQueue::setTranscodeTimeoutMs(int ms) {
    QPointer<DownloadWorker> worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, ms]() {
      if (worker) worker->setTranscodeTimeoutMs(ms);
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
    // A download folder that is not there (a drive not connected) says
    // nothing about the files in it.
    const bool rootThere = QFileInfo(m_store.root()).isDir();
    for (ItemManifest& m : m_catalog.items(m_serverId, m_userId)) {
      if (m.state == ItemState::Complete) {
        // Deleted, moved or cut short outside the app: download it again.
        if (rootThere && markIfMissing(m)) pending.append(m);
        continue;
      }
      // Killed between the rename and the manifest: the file is there and
      // has the expected size, so it is complete.
      const QString media = m_store.mediaPath(m);
      // A compressed copy's size is known only at the end; its .part is
      // renamed only once every segment is in.
      const bool compressed = m.quality == QLatin1String("compressed");
      const qint64 mediaSize = media.isEmpty() ? 0 : QFileInfo(media).size();
      if (!m.mediaComplete && !QFileInfo::exists(m_store.partPath(m)) && mediaSize > 0 &&
          (compressed || (m.expectedSize > 0 && mediaSize == m.expectedSize))) {
        if (compressed) {
          m.mediaSource = compressedMediaSource(m.mediaSource, m.transcode, mediaSize);
          m.expectedSize = mediaSize;
          m.transcode.segmentsDone = 0;
          m.transcode.partBytes = 0;
        }
        m.resumeValidator.clear();
        if (m.format != 1) m.mediaComplete = true;
        if (pendingAudio(m).isEmpty()) {
          m.state = ItemState::Complete;
          m.completedAt = QDateTime::currentDateTimeUtc();
          if (m_store.writeManifest(m)) m_catalog.update(m);
          continue;
        }
        if (m_store.writeManifest(m)) m_catalog.update(m);
      }
      if (m.mediaComplete && pendingAudio(m).isEmpty()) {
        // Killed after the last audio track, before the manifest said so.
        m.state = ItemState::Complete;
        m.completedAt = QDateTime::currentDateTimeUtc();
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
      entry.bytesOnDisk = m.mediaComplete ? m.expectedSize : QFileInfo(m_store.partPath(m)).size();
      entry.total = m.expectedSize + (m.mediaComplete ? pendingAudioBytes(m) : 0);
      // Failed after it was complete: its file went missing (markIfMissing).
      if (m.state == ItemState::Failed && m.completedAt.isValid()) entry.problem = QueueProblem::MediaMissing;
      m_order.append(m.itemId);
      m_entries.insert(m.itemId, std::move(entry));
    }
    Q_EMIT orderChanged();
  }

  bool DownloadQueue::markIfMissing(ItemManifest& m) {
    const QString media = m_store.mediaPath(m);
    const QFileInfo info(media);
    if (!media.isEmpty() && info.isFile() && (m.expectedSize <= 0 || info.size() == m.expectedSize))
      return false;
    if (info.exists())
      qCWarning(lcQueue) << "the downloaded file of" << m.itemId << "has" << info.size() << "bytes, not"
                         << m.expectedSize << "- it can be downloaded again";
    else
      qCWarning(lcQueue) << "the downloaded file of" << m.itemId << "is missing - it can be downloaded again";
    // completedAt stays: it tells a missing file from a failed download
    // after a restart.
    m.state = ItemState::Failed;
    m.mediaComplete = false;
    m.resumeValidator.clear();
    m.transcode.segmentsDone = 0;
    m.transcode.partBytes = 0;
    if (!m_store.writeManifest(m)) qCWarning(lcQueue) << "could not write the manifest of" << m.itemId;
    m_catalog.update(m);
    return true;
  }

  int DownloadQueue::verifyComplete() {
    if (m_serverId.isEmpty() || m_userId.isEmpty() || !QFileInfo(m_store.root()).isDir()) return 0;
    QList<ItemManifest> missing;
    for (ItemManifest& m : m_catalog.items(m_serverId, m_userId))
      if (m.state == ItemState::Complete && !m_entries.contains(m.itemId) && markIfMissing(m)) missing.append(m);
    std::stable_sort(missing.begin(), missing.end(),
                     [](const ItemManifest& a, const ItemManifest& b) { return a.queuedAt < b.queuedAt; });
    QStringList marked;
    for (const ItemManifest& m : std::as_const(missing)) {
      Entry entry;
      entry.manifest = m;
      entry.total = m.expectedSize;
      entry.problem = QueueProblem::MediaMissing;
      m_order.append(m.itemId);
      m_entries.insert(m.itemId, std::move(entry));
      marked.append(m.itemId);
    }
    if (marked.isEmpty()) return 0;
    Q_EMIT orderChanged();
    for (const QString& itemId : std::as_const(marked)) {
      Q_EMIT itemChanged(itemId);
      Q_EMIT failed(itemId, QueueProblem::MediaMissing);
    }
    return int(marked.size());
  }

  EnqueueResult DownloadQueue::enqueueOriginal(const QJsonObject& item,
                                               const QJsonObject& mediaSource,
                                               const QJsonObject& userData,
                                               const QStringList& parents) {
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
    for (const QString& parent : parents)
      if (DownloadStore::isSafeId(parent) && !m.parents.contains(parent)) m.parents.append(parent);
    m.format = ItemManifest::FormatVersion;
    m.mediaFile = m_store.libraryFileFor(item, itemId, mediaExtension(mediaSource), takenLibraryFiles());
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

  EnqueueResult DownloadQueue::enqueueCompressed(const QJsonObject& item,
                                                 const QJsonObject& mediaSource,
                                                 const QString& presetId,
                                                 std::optional<int> audioStreamIndex,
                                                 const QJsonObject& userData,
                                                 const QStringList& parents) {
    if (m_serverId.isEmpty() || m_userId.isEmpty()) return EnqueueResult::NotSignedIn;
    const QString itemId = item.value(QLatin1String("Id")).toString();
    const QString sourceId = mediaSource.value(QLatin1String("Id")).toString();
    if (!DownloadStore::isSafeId(itemId) || !DownloadStore::isSafeId(sourceId))
      return EnqueueResult::InvalidItem;
    const auto preset = compressedPreset(presetId);
    const bool audio = item.value(QLatin1String("MediaType")).toString() == QLatin1String("Audio");
    if (!preset || preset->profile.audio != audio) return EnqueueResult::InvalidItem;
    if (m_catalog.find(m_serverId, m_userId, itemId)) return EnqueueResult::AlreadyThere;

    ItemManifest m;
    m.serverId = m_serverId;
    m.userId = m_userId;
    m.itemId = itemId;
    m.state = ItemState::Queued;
    m.quality = QStringLiteral("compressed");
    m.transcode.preset = preset->id;
    m.transcode.audioStreamIndex = audioStreamIndex.value_or(-1);
    m.item = item;
    m.mediaSource = mediaSource;
    m.userData = userData;
    for (const QString& parent : parents)
      if (DownloadStore::isSafeId(parent) && !m.parents.contains(parent)) m.parents.append(parent);
    // fMP4: the init segment and the media segments, joined.
    m.format = ItemManifest::FormatVersion;
    m.mediaFile = m_store.libraryFileFor(item, itemId, QStringLiteral("mp4"), takenLibraryFiles());
    double ticks = mediaSource.value(QLatin1String("RunTimeTicks")).toDouble();
    if (ticks <= 0) ticks = item.value(QLatin1String("RunTimeTicks")).toDouble();
    m.expectedSize = api::estimatedCompressedSize(preset->profile, static_cast<std::int64_t>(ticks));
    return admit(std::move(m));
  }

  EnqueueResult DownloadQueue::admit(ItemManifest m) {
    m.queuedAt = QDateTime::currentDateTimeUtc();
    if (!m_order.isEmpty()) {
      const QDateTime last = m_entries.value(m_order.last()).manifest.queuedAt;
      if (last.isValid() && m.queuedAt <= last) m.queuedAt = last.addMSecs(1);
    }
    if (!m_store.writeManifest(m)) return EnqueueResult::DiskError;
    // What was stored, secrets removed, is what the queue keeps.
    if (const auto stored = m_store.readManifest(m.serverId, m.userId, m.itemId)) m = *stored;
    m_catalog.update(m);

    const QString itemId = m.itemId;
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

  QSet<QString> DownloadQueue::takenLibraryFiles() const {
    QSet<QString> taken;
    for (const ItemManifest& m : m_catalog.all()) {
      if (m.format == 1) continue;
      taken.insert(m.mediaFile.toLower());
      for (const QString& file : m.subtitleFiles) taken.insert(file.toLower());
      for (const QString& file : m.audioFiles) taken.insert(file.toLower());
    }
    return taken;
  }

  bool DownloadQueue::compressedActive() const {
    for (const Entry& entry : m_entries)
      if (entry.activity != Activity::Idle && entry.manifest.quality == QLatin1String("compressed"))
        return true;
    return false;
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
    if (stopsQueue(m_problem) || m_serverUrl.isEmpty() || !m_worker) return;
    int active = activeCount();
    // The first reason an item had to wait, for the whole queue's notice.
    QueueProblem waiting = QueueProblem::None;
    // The free space is the same for every item of this pass: an item
    // needing at least as much as one refused for low space is refused too,
    // without asking the disk again.
    std::int64_t refusedLowSpace = -1;
    for (const QString& itemId : std::as_const(m_order)) {
      if (active >= m_maxParallel) break;
      Entry& entry = m_entries[itemId];
      if (entry.manifest.state != ItemState::Queued || entry.activity != Activity::Idle) continue;
      // One transcode at a time on the server.
      if (entry.manifest.quality == QLatin1String("compressed") && compressedActive()) continue;
      const std::int64_t remaining =
          entry.manifest.mediaComplete
              ? pendingAudioBytes(entry.manifest)
              : std::max<std::int64_t>(0, entry.manifest.expectedSize - entry.bytesOnDisk);
      StorageVerdict verdict = StorageVerdict::Ok;
      if (refusedLowSpace >= 0 && remaining >= refusedLowSpace)
        verdict = StorageVerdict::LowSpace;
      else if (m_guard)
        verdict = m_guard->admit(m_store.root(), remaining, committedBytes(entry));
      if (verdict != StorageVerdict::Ok) {
        // This one waits; smaller ones behind it may still fit.
        const QueueProblem why =
            verdict == StorageVerdict::OverLimit ? QueueProblem::OverLimit : QueueProblem::LowSpace;
        if (why == QueueProblem::LowSpace && (refusedLowSpace < 0 || remaining < refusedLowSpace))
          refusedLowSpace = remaining;
        if (waiting == QueueProblem::None) waiting = why;
        if (entry.problem != why) {
          entry.problem = why;
          Q_EMIT itemChanged(itemId);
        }
        continue;
      }
      entry.problem = QueueProblem::None;
      startTransfer(entry);
      ++active;
    }
    if (waiting != QueueProblem::None)
      setProblem(waiting);
    else if (isSpaceProblem(m_problem))
      setProblem(QueueProblem::None);
    if (waiting != QueueProblem::None && !m_spaceTimer->isActive())
      m_spaceTimer->start();
    else if (waiting == QueueProblem::None)
      m_spaceTimer->stop();
  }

  void DownloadQueue::recheckSpace() { schedule(); }

  void DownloadQueue::startTransfer(Entry& entry) {
    const ItemManifest& m = entry.manifest;
    DownloadJob job;
    job.key = m.serverId + QLatin1Char('/') + m.userId + QLatin1Char('/') + m.itemId;
    if (m.mediaComplete) {
      const QList<QJsonObject> audio = pendingAudio(m);
      if (audio.isEmpty()) {
        complete(entry);
        return;
      }
      const QJsonObject stream = audio.first();
      const int index = stream.value(QLatin1String("Index")).toInt();
      const QString sourceId = m.mediaSource.value(QLatin1String("Id")).toString(m.itemId);
      const QString base = m.mediaFile.left(m.mediaFile.size() - QFileInfo(m.mediaFile).suffix().size() - 1);
      QStringList used = m.subtitleFiles.values() + m.audioFiles.values();
      const QString file = sidecarPath(
          base, sidecarSuffix(stream, m.mediaSource.value(QLatin1String("Path")).toString()),
          QStringLiteral("mka"), used);
      job.url = api::externalAudioDownloadUrl(m_serverUrl, m.itemId, sourceId, index,
                                              QUuid::createUuid().toString(QUuid::Id128));
      job.serverUrl = m_serverUrl;
      job.authorization = m_authorization;
      job.partPath = m_store.itemDir(m) + QStringLiteral("/audio-%1.part").arg(index);
      job.finalPath = m_store.filePath(m, file);
      job.audioOnly = true;
      job.audioLanguage = stream.value(QLatin1String("Language")).toString();
      static const QRegularExpression iso(QStringLiteral("^[a-z]{3}$"));
      if (!iso.match(job.audioLanguage).hasMatch()) job.audioLanguage.clear();
      entry.audioRunning = index;
      entry.activity = Activity::Transferring;
      entry.afterStop = Entry::AfterStop::Requeue;
      entry.bytesOnDisk = m.expectedSize;
      entry.total = m.expectedSize + pendingAudioBytes(m);
      m_pendingAudioFile.insert(m.itemId, file);
      QPointer<DownloadWorker> worker = m_worker;
      QMetaObject::invokeMethod(worker, [worker, job]() {
        if (worker) worker->start(job);
      });
      Q_EMIT itemChanged(m.itemId);
      return;
    }
    entry.audioRunning = -1;
    job.url = api::originalDownloadUrl(m_serverUrl,
                                       m.mediaSource.value(QLatin1String("Id")).toString(m.itemId));
    job.serverUrl = m_serverUrl;
    job.authorization = m_authorization;
    job.partPath = m_store.partPath(m);
    job.finalPath = m_store.mediaPath(m);
    job.expectedSize = m.expectedSize;
    job.resumeValidator = m.resumeValidator;
    if (m.quality == QLatin1String("compressed")) {
      const auto preset = compressedPreset(m.transcode.preset);
      if (!preset) {
        entry.manifest.state = ItemState::Failed;
        entry.problem = QueueProblem::Refused;
        save(entry);
        Q_EMIT itemChanged(m.itemId);
        Q_EMIT failed(m.itemId, QueueProblem::Refused);
        return;
      }
      DownloadJob::Transcode transcode;
      transcode.playbackInfoUrl = api::playbackInfoUrl(m_serverUrl, m.itemId, m.userId);
      const int audio = m.transcode.audioStreamIndex;
      transcode.playbackInfoBody = api::compressedPlaybackInfoBody(
          m.userId, m.mediaSource.value(QLatin1String("Id")).toString(m.itemId), preset->profile,
          audio >= 0 ? std::optional<int>(audio) : std::nullopt);
      transcode.resume = m.transcode;
      transcode.estimatedSize = m.expectedSize;
      job.transcode = transcode;
      // The size is checked segment by segment, not against an estimate.
      job.expectedSize = 0;
    }
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
    if (stopsQueue(problem)) {
      qCInfo(lcQueue) << "queue stopped:" << static_cast<int>(problem);
      stopAllTransfers();
    } else if (problem != QueueProblem::None) {
      qCInfo(lcQueue) << "items wait for space:" << static_cast<int>(problem);
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
    removeEntry(itemId);
    Q_EMIT itemChanged(itemId);
    schedule();
  }

  void DownloadQueue::removeEntry(const QString& itemId) {
    const auto it = m_entries.constFind(itemId);
    if (it == m_entries.cend()) return;
    const ItemManifest manifest = it->manifest;
    dropEntry(itemId);
    m_store.removeItem(manifest);
    m_catalog.remove(manifest.serverId, manifest.userId, manifest.itemId);
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
      if (it->audioRunning >= 0) {
        const int all = externalAudioCount(it->manifest);
        s.audioTracks = all;
        s.audioTrack = std::min(all, int(it->manifest.audioFiles.size() + it->manifest.audioSkipped.size()) + 1);
      }
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

  void DownloadQueue::mediaDone(Entry& entry) {
    ItemManifest& done = entry.manifest;
    done.resumeValidator.clear();
    const qint64 size = QFileInfo(m_store.mediaPath(done)).size();
    if (size > 0) done.expectedSize = size;
    if (done.quality == QLatin1String("compressed")) {
      // From now on the manifest describes the copy, not the server's file.
      done.mediaSource = compressedMediaSource(done.mediaSource, done.transcode, size);
      done.transcode.segmentsDone = 0;
      done.transcode.partBytes = 0;
    }
    if (done.format != 1) done.mediaComplete = true;
    entry.bytesOnDisk = done.expectedSize;
    if (!pendingAudio(done).isEmpty()) {
      // The external audio tracks come next, as the same item.
      entry.attempts = 0;
      entry.total = done.expectedSize + pendingAudioBytes(done);
      save(entry);
      Q_EMIT itemChanged(done.itemId);
      return;
    }
    complete(entry);
  }

  void DownloadQueue::complete(Entry& entry) {
    const QString itemId = entry.manifest.itemId;
    entry.manifest.state = ItemState::Complete;
    entry.manifest.completedAt = QDateTime::currentDateTimeUtc();
    save(entry);
    dropEntry(itemId);
    Q_EMIT itemChanged(itemId);
    Q_EMIT completed(itemId);
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
    if (entry.audioRunning >= 0) return;
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

  void DownloadQueue::onTranscodeStarted(const QString& key, const QJsonObject& state) {
    const QString itemId = itemOf(key, m_serverId, m_userId);
    const auto it = m_entries.find(itemId);
    if (itemId.isEmpty() || it == m_entries.end()) return;
    TranscodeState& t = it->manifest.transcode;
    const int audio = state.value(QLatin1String("audioStreamIndex")).toInt(-1);
    // The server's choice of audio is kept, so a resumed copy asks for the
    // same track.
    if (audio >= 0) t.audioStreamIndex = audio;
    t.videoCodec = state.value(QLatin1String("videoCodec")).toString();
    t.audioCodec = state.value(QLatin1String("audioCodec")).toString();
    t.segmentCount = state.value(QLatin1String("segmentCount")).toInt();
    t.signature = state.value(QLatin1String("signature")).toString();
    t.initHash = state.value(QLatin1String("initHash")).toString();
    save(*it);
    Q_EMIT itemChanged(itemId);
  }

  void DownloadQueue::onSegmentWritten(const QString& key, int segmentsDone, qint64 partBytes) {
    const QString itemId = itemOf(key, m_serverId, m_userId);
    const auto it = m_entries.find(itemId);
    if (itemId.isEmpty() || it == m_entries.end()) return;
    TranscodeState& t = it->manifest.transcode;
    if (t.segmentsDone == segmentsDone && t.partBytes == partBytes) return;
    t.segmentsDone = segmentsDone;
    t.partBytes = partBytes;
    it->bytesOnDisk = partBytes;
    // Every segment: a restart continues after the last one written.
    save(*it);
  }

  void DownloadQueue::onProgress(const QString& key, qint64 bytesOnDisk, qint64 total) {
    const QString itemId = itemOf(key, m_serverId, m_userId);
    const auto it = m_entries.find(itemId);
    if (itemId.isEmpty() || it == m_entries.end()) return;
    if (it->audioRunning >= 0) {
      it->bytesOnDisk = it->manifest.expectedSize + bytesOnDisk;
      it->total = std::max(it->total, it->bytesOnDisk);
      Q_EMIT progress(itemId, it->bytesOnDisk, it->total);
      return;
    }
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
    const int audio = entry.audioRunning;
    entry.audioRunning = -1;
    entry.bytesOnDisk = audio >= 0 ? entry.manifest.expectedSize
                                   : QFileInfo(m_store.partPath(entry.manifest)).size();
    const QString audioFile = m_pendingAudioFile.take(itemId);
    // Cancelled while the transfer was ending: whatever it ended with
    // (complete too), the item goes.
    if (entry.afterStop == Entry::AfterStop::Remove) {
      removeEntry(itemId);
      Q_EMIT itemChanged(itemId);
      schedule();
      return;
    }
    // Paused while it was ending: what is in stays, the rest waits.
    const auto pauseIfAsked = [this, itemId]() {
      const auto again = m_entries.find(itemId);
      if (again == m_entries.end() || again->afterStop != Entry::AfterStop::Pause ||
          again->manifest.state != ItemState::Queued)
        return;
      again->manifest.state = ItemState::Paused;
      save(*again);
      Q_EMIT itemChanged(itemId);
    };
    if (audio >= 0) {
      switch (outcome) {
      case TransferOutcome::Complete:
        entry.manifest.audioFiles.insert(audio, audioFile);
        entry.attempts = 0;
        save(entry);
        Q_EMIT itemChanged(itemId);
        pauseIfAsked();
        schedule();
        return;
      case TransferOutcome::Refused:
      case TransferOutcome::NotServerUrl:
        // This server will not give the track (no transcoding allowed, a
        // file it cannot read): the item completes without it.
        qCInfo(lcQueue) << "external audio" << audio << "of" << itemId << "skipped";
        entry.manifest.audioSkipped.append(audio);
        entry.attempts = 0;
        save(entry);
        Q_EMIT itemChanged(itemId);
        schedule();
        return;
      default:
        break;
      }
    }

    const auto fail = [&](QueueProblem problem) {
      entry.manifest.state = ItemState::Failed;
      entry.problem = problem;
      save(entry);
      Q_EMIT itemChanged(itemId);
      Q_EMIT failed(itemId, problem);
    };

    switch (outcome) {
    case TransferOutcome::Complete:
      mediaDone(entry);
      pauseIfAsked();
      break;
    case TransferOutcome::Cancelled:
      if (entry.afterStop == Entry::AfterStop::Pause) {
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
      // The disk filled while it was written: it waits for space like an
      // item that does not fit (schedule() below looks again, and the
      // others go on while they fit).
      entry.problem = QueueProblem::LowSpace;
      Q_EMIT itemChanged(itemId);
      break;
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
        entry.manifest.transcode.segmentsDone = 0;
        entry.manifest.transcode.partBytes = 0;
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
