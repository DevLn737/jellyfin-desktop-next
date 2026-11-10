#pragma once

#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/DownloadWorker.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/StorageGuard.h>

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <cstdint>
#include <memory>

class QThread;
class QTimer;

// The download queue of the signed-in user (GUI thread). It keeps the order,
// the state of each item and the retries, writes the manifests and hands the
// transfers to a DownloadWorker on its own thread.
//
// - One download at a time by default (1-3).
// - Everything not complete is resumed after a restart, in the order it was
//   queued (the manifests remember it).
// - A dropped transfer is retried 5 times, after 2, 4, 8, 16 and 32 s; a
//   retry that received data starts the count again. Then the item fails;
//   retry() puts it back.
// - 401 stops the queue until the next sign-in (setAccess); low disk space
//   or the space limit stop it until resumeAll().
// - Other users' downloads stay on disk untouched (and are not resumed)
//   until that user signs in again.
namespace jfd::offline {

  enum class QueueProblem {
    None,
    // resumeAll() continues.
    PausedByUser,
    // The server answered 401; continues after the next setAccess().
    SignInRequired,
    // The reserve of free space would be broken.
    LowSpace,
    // The downloads would use more than the limit.
    OverLimit,
    // Per item: the server refused (403 without download permission, 404).
    Refused,
    // Per item: retries used up.
    Network,
    // Per item: writing to the download folder failed.
    DiskError,
    // Per item: the download URL or a redirect led off the server.
    NotServerUrl,
  };

  enum class Activity {
    // Waiting its turn (or paused, failed, complete: see the state).
    Idle,
    Transferring,
    // Between retries.
    RetryWait,
  };

  struct DownloadStatus {
    ItemState state = ItemState::Queued;
    Activity activity = Activity::Idle;
    std::int64_t bytesOnDisk = 0;
    std::int64_t total = 0;
    // Why it failed.
    QueueProblem problem = QueueProblem::None;
  };

  enum class EnqueueResult {
    Queued,
    AlreadyThere,
    NotSignedIn,
    // A folder, an ISO, a remote or .strm source (api::originalAvailable).
    OriginalUnavailable,
    // No item or media source id, or one that is not a safe folder name.
    InvalidItem,
    DiskError,
  };

  class DownloadQueue : public QObject {
    Q_OBJECT

  public:
    DownloadQueue(DownloadStore& store, OfflineCatalog& catalog,
                  std::shared_ptr<StorageGuard> guard, QObject* parent = nullptr);
    ~DownloadQueue() override;

    // The signed-in session: whose downloads run, against which server, with
    // which Authorization header (kept in memory only). Takes up that user's
    // unfinished downloads from the catalog. An empty serverUrl (signed out)
    // stops the transfers; the items stay queued.
    void setAccess(const QString& serverId, const QString& userId, const QUrl& serverUrl,
                   const QByteArray& authorization);

    void setMaxParallel(int count);
    int maxParallel() const { return m_maxParallel; }
    void setRetryDelaysMs(QList<int> delays) { m_retryDelays = std::move(delays); }
    // For tests: how often the worker asks the guard while writing.
    void setCheckEveryBytes(std::int64_t bytes);
    // A transfer with no data for this long is dropped and retried (60 s).
    void setStallTimeoutMs(int ms);

    // Queues the original of `mediaSource` (one of the item's versions).
    // `item`, `mediaSource` and `userData` are the server's JSON; secrets
    // are removed before they are stored.
    EnqueueResult enqueueOriginal(const QJsonObject& item, const QJsonObject& mediaSource,
                                  const QJsonObject& userData = {});

    void pause(const QString& itemId);
    void resume(const QString& itemId);
    // Stops it and deletes everything downloaded for it.
    void cancel(const QString& itemId);
    void retry(const QString& itemId);
    // Moves an item to `index` in the queue order.
    void move(const QString& itemId, int index);
    void pauseAll();
    void resumeAll();

    // The signed-in user's items that are not complete, in queue order.
    QStringList order() const { return m_order; }
    std::optional<DownloadStatus> status(const QString& itemId) const;
    // Why the whole queue stands still; None while it runs.
    QueueProblem problem() const { return m_problem; }
    // Transferring or waiting for a retry.
    int activeCount() const;

  Q_SIGNALS:
    void itemChanged(const QString& itemId);
    void progress(const QString& itemId, qint64 bytesOnDisk, qint64 total);
    void completed(const QString& itemId);
    void failed(const QString& itemId, jfd::offline::QueueProblem problem);
    void problemChanged(jfd::offline::QueueProblem problem);
    void orderChanged();

  private:
    struct Entry {
      ItemManifest manifest;
      Activity activity = Activity::Idle;
      int attempts = 0;
      std::int64_t bytesOnDisk = 0;
      std::int64_t total = 0;
      QueueProblem problem = QueueProblem::None;
      // What to do when the running transfer reports Cancelled.
      enum class AfterStop { Requeue, Pause, Remove } afterStop = AfterStop::Requeue;
      QTimer* retryTimer = nullptr;
    };

    void reload();
    void schedule();
    void startTransfer(Entry& entry);
    void stopTransfer(Entry& entry, Entry::AfterStop after);
    void stopAllTransfers();
    void setProblem(QueueProblem problem);
    void save(Entry& entry);
    void dropEntry(const QString& itemId);
    void renumber();
    std::int64_t committedBytes(const Entry& next) const;

    void onStarted(const QString& key, qint64 offset, qint64 total, const QString& validator);
    void onProgress(const QString& key, qint64 bytesOnDisk, qint64 total);
    void onFinished(const QString& key, int outcome, qint64 received);

    DownloadStore& m_store;
    OfflineCatalog& m_catalog;
    std::shared_ptr<StorageGuard> m_guard;
    QThread* m_thread = nullptr;
    QPointer<DownloadWorker> m_worker;

    QString m_serverId;
    QString m_userId;
    QUrl m_serverUrl;
    QByteArray m_authorization;

    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QueueProblem m_problem = QueueProblem::None;
    int m_maxParallel = 1;
    QList<int> m_retryDelays{2000, 4000, 8000, 16000, 32000};
  };

} // namespace jfd::offline
