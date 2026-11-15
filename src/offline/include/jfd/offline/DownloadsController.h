#pragma once

#include <jfd/api/JellyfinClient.h>
#include <jfd/offline/DownloadListModel.h>
#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/StorageGuard.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <optional>

// What the screens do with downloads (stage 3.10a, the interface step):
// "Download" on an item, a season, a series, an album or a playlist (with
// the total shown before anything is queued), the state of each item for
// the badges on cards, the queue and the downloaded items as lists, the
// space they take, and deleting them. Qt Core only; the QML singleton
// `Downloads` (src/ui/library) forwards to it.
//
// Originals (Items/{id}/Download) by default; with the "Download quality"
// setting on a compressed preset, compressed copies the server transcodes
// (stage 3.10b, CompressedDownload.h). A user without
// EnableContentDownloading gets no "Download" (allowed() is false and the
// screens say why); a compressed-only "Download" for such users, and a
// quality choice in the dialog, are a later step.
namespace jfd::offline {

  class DownloadsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY userChanged)
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    // The server said whether this user may download files.
    Q_PROPERTY(bool permissionKnown READ permissionKnown NOTIFY permissionChanged)
    // ... and that it may (EnableContentDownloading).
    Q_PROPERTY(bool allowed READ allowed NOTIFY permissionChanged)
    Q_PROPERTY(jfd::offline::DownloadListModel* queue READ queueModel CONSTANT)
    Q_PROPERTY(jfd::offline::DownloadListModel* downloaded READ downloadedModel CONSTANT)
    // Goes up whenever the state of any download changes (not on progress):
    // bindings that call stateOf() depend on it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    // Goes up with every progress report: bindings that call progressOf().
    Q_PROPERTY(int progressRevision READ progressRevision NOTIFY progressRevisionChanged)
    Q_PROPERTY(bool hasDownloads READ hasDownloads NOTIFY revisionChanged)
    // The whole queue stands still (paused, low space, sign-in needed).
    Q_PROPERTY(bool queueStopped READ queueStopped NOTIFY queueProblemChanged)
    Q_PROPERTY(bool queuePausedByUser READ queuePausedByUser NOTIFY queueProblemChanged)
    Q_PROPERTY(QString queueProblemText READ queueProblemText NOTIFY queueProblemChanged)
    // Bytes on disk (complete files and the parts of the queued ones), the
    // limit (0: none), the reserve, and the free space of the download
    // folder's disk (-1: unknown, a disconnected drive).
    Q_PROPERTY(qint64 usedBytes READ usedBytes NOTIFY storageChanged)
    Q_PROPERTY(qint64 limitBytes READ limitBytes NOTIFY storageChanged)
    Q_PROPERTY(qint64 reserveBytes READ reserveBytes NOTIFY storageChanged)
    Q_PROPERTY(qint64 freeBytes READ freeBytes NOTIFY storageChanged)
    // Watched downloads, what "Delete watched" frees.
    Q_PROPERTY(qint64 watchedBytes READ watchedBytes NOTIFY storageChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)

  public:
    DownloadsController(DownloadStore& store, OfflineCatalog& catalog, DownloadQueue& queue,
                        StorageGuard& guard, QObject* parent = nullptr);
    ~DownloadsController() override;

    // The server connection the "Download" requests go through; null: none.
    void setClient(api::JellyfinClient* client);
    // The signed-in user (empty: signed out). Rebuilds the lists and, when
    // online, asks the server for the user's download permission.
    void setUser(const QString& serverId, const QString& userId);
    void setOnline(bool online);
    // The "Download quality" setting ("original" or a video preset id):
    // what the next prepare() plans.
    void setQuality(const QString& quality) { m_quality = quality; }
    QString quality() const { return m_quality; }
    // Set by the permission request; public for tests.
    void setPermission(bool known, bool allowed);
    // Rebuilds the lists from the catalog (after a sync changed manifests).
    void refresh();

    bool signedIn() const { return !m_serverId.isEmpty() && !m_userId.isEmpty(); }
    bool online() const { return m_online; }
    bool permissionKnown() const { return m_permissionKnown; }
    bool allowed() const { return m_permissionKnown && m_allowed; }
    DownloadListModel* queueModel() { return &m_queueModel; }
    DownloadListModel* downloadedModel() { return &m_downloadedModel; }
    int revision() const { return m_revision; }
    int progressRevision() const { return m_progressRevision; }
    bool hasDownloads() const { return !m_members.isEmpty(); }
    bool queueStopped() const;
    bool queuePausedByUser() const;
    QString queueProblemText() const;
    qint64 usedBytes() const { return m_usedBytes; }
    qint64 limitBytes() const { return m_guard.limitBytes(); }
    qint64 reserveBytes() const { return m_guard.reserveBytes(); }
    qint64 freeBytes() const { return m_freeBytes; }
    qint64 watchedBytes() const { return m_watchedBytes; }
    QString folder() const { return m_store.root(); }

    // The signed-in user's complete downloads, by series or album, season
    // and number, then name.
    QList<ItemManifest> completeItems() const;
    // The item's own downloaded image (Primary, else Thumb) as a file URL;
    // empty when it has none.
    QUrl imageUrlOf(const ItemManifest& manifest) const;

    // The state of an item, or of a series, season, album or playlist from
    // its downloaded items: "" (nothing), "queued", "downloading",
    // "paused", "failed" or "complete".
    Q_INVOKABLE QString stateOf(const QString& itemId) const;
    // 0..1 of what is being downloaded for it.
    Q_INVOKABLE double progressOf(const QString& itemId) const;
    // Of a series, season, album or playlist: {count, complete, bytes}.
    Q_INVOKABLE QVariantMap groupSummary(const QString& parentId) const;
    // Bytes on disk for an item or a group.
    Q_INVOKABLE qint64 bytesOf(const QString& itemId) const;

    // Asks the server what "Download" on `itemId` takes. Answers with
    // prepared(requestId, summary) or prepareFailed(requestId, text);
    // nothing is queued until confirm(). The summary:
    //   title, type, count (new items), alreadyCount, unavailableCount,
    //   bytes, single (one item), versions [{name, bytes}] (a single item
    //   with several versions), fits (the space limits allow it),
    //   spaceText (why not).
    Q_INVOKABLE int prepare(const QString& itemId, bool unplayedOnly = false);
    // Queues what prepare() found; `version` picks the media source of a
    // single item. Returns how many were queued.
    Q_INVOKABLE int confirm(int requestId, int version = 0);
    Q_INVOKABLE void discard(int requestId);

    Q_INVOKABLE void pause(const QString& itemId);
    Q_INVOKABLE void resume(const QString& itemId);
    Q_INVOKABLE void cancel(const QString& itemId);
    Q_INVOKABLE void retry(const QString& itemId);
    Q_INVOKABLE void moveUp(const QString& itemId);
    Q_INVOKABLE void moveDown(const QString& itemId);
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();

    // Deletes a download (stops it first) or every download of a group.
    // False when a file could not be deleted (on Windows, while it plays).
    Q_INVOKABLE bool remove(const QString& itemId);
    // Returns how many were deleted.
    Q_INVOKABLE int removeWatched();
    Q_INVOKABLE int removeAll();

    // "1.4 GiB", "350 MiB".
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    static QString formatBytesText(qint64 bytes);
    static QString problemText(QueueProblem problem);

  Q_SIGNALS:
    void userChanged();
    void onlineChanged();
    void permissionChanged();
    void revisionChanged();
    void progressRevisionChanged();
    void queueProblemChanged();
    void storageChanged();
    void prepared(int requestId, const QVariantMap& summary);
    void prepareFailed(int requestId, const QString& text);
    // For the shell's toast.
    void message(const QString& text, bool error);

  private:
    struct Candidate {
      QJsonObject item;
      // The versions whose original can be downloaded; for a compressed
      // copy every version (the server transcodes any of them).
      QJsonArray sources;
      // Empty: the original. Otherwise the preset of a compressed copy.
      QString preset;
    };
    struct Plan {
      QString parentId;
      QList<Candidate> items;
    };

    // The item's status while it is in the queue (not complete).
    std::optional<DownloadStatus> queuedStatus(const QString& itemId) const;
    void askPermission();
    void scheduleRebuild();
    void rebuild();
    void updateStorage();
    void prepareItems(int requestId, const QString& parentId, const QString& title,
                      const QString& type, const QJsonArray& items);
    QStringList membersOf(const QString& id) const;
    QString statusTextFor(const ItemManifest& manifest, const DownloadStatus* status) const;
    DownloadListModel::Row rowFor(const ItemManifest& manifest, const DownloadStatus* status) const;
    bool removeOne(const QString& itemId);

    DownloadStore& m_store;
    OfflineCatalog& m_catalog;
    DownloadQueue& m_queue;
    StorageGuard& m_guard;
    QPointer<api::JellyfinClient> m_client;
    QString m_serverId;
    QString m_userId;
    bool m_online = false;
    bool m_permissionKnown = false;
    bool m_allowed = false;
    QString m_quality = QStringLiteral("original");
    quint64 m_permissionAttempt = 0;

    DownloadListModel m_queueModel;
    DownloadListModel m_downloadedModel;
    // Item id -> the item's manifest state, for the signed-in user.
    QHash<QString, ItemManifest> m_members;
    // Series, season, album or playlist id -> its items.
    QHash<QString, QStringList> m_groups;
    int m_revision = 0;
    int m_progressRevision = 0;
    qint64 m_usedBytes = 0;
    qint64 m_freeBytes = -1;
    qint64 m_watchedBytes = 0;
    QTimer m_rebuild;

    int m_nextRequest = 1;
    QHash<int, Plan> m_plans;
  };

} // namespace jfd::offline
