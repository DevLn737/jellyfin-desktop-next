#pragma once

#include <jfd/library/ItemListModel.h>
#include <jfd/offline/DownloadListModel.h>
#include <jfd/offline/DownloadsController.h>

#include <QJSEngine>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>
#include <qqmlintegration.h>

#include <functional>

namespace jfd::ui {

  class Navigator;

  // The download screens' way into the offline module (QML: `Downloads`):
  // the "Download" buttons and their dialog, the badges on cards, the
  // Downloads page (queue, downloaded items, space), and the home screen's
  // rows of downloaded items when the server cannot be reached.
  //
  // The application sets the controller before the window loads
  // (setService); without one `available` is false and the screens show no
  // download controls.
  class DownloadsUi : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Downloads)
    QML_SINGLETON
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(bool permissionKnown READ permissionKnown NOTIFY changed)
    // The signed-in user may download (EnableContentDownloading).
    Q_PROPERTY(bool allowed READ allowed NOTIFY changed)
    Q_PROPERTY(jfd::offline::DownloadListModel* queue READ queue NOTIFY changed)
    Q_PROPERTY(jfd::offline::DownloadListModel* downloaded READ downloaded NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int progressRevision READ progressRevision NOTIFY progressChanged)
    Q_PROPERTY(bool hasDownloads READ hasDownloads NOTIFY changed)
    Q_PROPERTY(bool queueStopped READ queueStopped NOTIFY changed)
    Q_PROPERTY(bool queuePausedByUser READ queuePausedByUser NOTIFY changed)
    Q_PROPERTY(QString queueProblemText READ queueProblemText NOTIFY changed)
    Q_PROPERTY(qint64 usedBytes READ usedBytes NOTIFY changed)
    Q_PROPERTY(qint64 limitBytes READ limitBytes NOTIFY changed)
    Q_PROPERTY(qint64 reserveBytes READ reserveBytes NOTIFY changed)
    Q_PROPERTY(qint64 freeBytes READ freeBytes NOTIFY changed)
    Q_PROPERTY(qint64 watchedBytes READ watchedBytes NOTIFY changed)
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    // The offline home's rows, as card lists (ItemCard roles).
    Q_PROPERTY(jfd::library::ItemListModel* resumeRow READ resumeRow CONSTANT)
    Q_PROPERTY(jfd::library::ItemListModel* moviesRow READ moviesRow CONSTANT)
    Q_PROPERTY(jfd::library::ItemListModel* episodesRow READ episodesRow CONSTANT)
    Q_PROPERTY(jfd::library::ItemListModel* musicRow READ musicRow CONSTANT)
    Q_PROPERTY(jfd::library::ItemListModel* otherRow READ otherRow CONSTANT)
    // Downloaded tracks can play without the server (setTrackListHandler).
    Q_PROPERTY(bool canPlayTracks READ canPlayTracks NOTIFY changed)

  public:
    // Plays downloaded tracks (the server's item JSON) from `start`; false
    // when music cannot play now.
    using TrackListHandler = std::function<bool(const QList<api::Item>& tracks, int start)>;

    explicit DownloadsUi(QObject* parent = nullptr);
    ~DownloadsUi() override;

    // The application owns the controller; null detaches.
    static void setService(offline::DownloadsController* service);
    static offline::DownloadsController* service() { return s_service; }
    static void setTrackListHandler(TrackListHandler handler);
    static DownloadsUi* create(QQmlEngine*, QJSEngine*);
    // The "downloads" route and its sidebar entry (order 250).
    static void registerRoutes(Navigator& navigator);

    bool available() const { return s_service != nullptr; }
    bool signedIn() const { return s_service != nullptr && s_service->signedIn(); }
    bool online() const { return s_service != nullptr && s_service->online(); }
    bool permissionKnown() const { return s_service != nullptr && s_service->permissionKnown(); }
    bool allowed() const { return s_service != nullptr && s_service->allowed(); }
    offline::DownloadListModel* queue() const { return s_service ? s_service->queueModel() : nullptr; }
    offline::DownloadListModel* downloaded() const {
      return s_service ? s_service->downloadedModel() : nullptr;
    }
    int revision() const { return s_service ? s_service->revision() : 0; }
    int progressRevision() const { return s_service ? s_service->progressRevision() : 0; }
    bool hasDownloads() const { return s_service != nullptr && s_service->hasDownloads(); }
    bool queueStopped() const { return s_service != nullptr && s_service->queueStopped(); }
    bool queuePausedByUser() const { return s_service != nullptr && s_service->queuePausedByUser(); }
    QString queueProblemText() const { return s_service ? s_service->queueProblemText() : QString(); }
    qint64 usedBytes() const { return s_service ? s_service->usedBytes() : 0; }
    qint64 limitBytes() const { return s_service ? s_service->limitBytes() : 0; }
    qint64 reserveBytes() const { return s_service ? s_service->reserveBytes() : 0; }
    qint64 freeBytes() const { return s_service ? s_service->freeBytes() : -1; }
    qint64 watchedBytes() const { return s_service ? s_service->watchedBytes() : 0; }
    QString folder() const { return s_service ? s_service->folder() : QString(); }
    library::ItemListModel* resumeRow() { return &m_resume; }
    library::ItemListModel* moviesRow() { return &m_movies; }
    library::ItemListModel* episodesRow() { return &m_episodes; }
    library::ItemListModel* musicRow() { return &m_music; }
    library::ItemListModel* otherRow() { return &m_other; }
    bool canPlayTracks() const { return bool(s_tracks); }

    Q_INVOKABLE QString stateOf(const QString& itemId) const;
    Q_INVOKABLE double progressOf(const QString& itemId) const;
    Q_INVOKABLE QVariantMap groupSummary(const QString& parentId) const;
    Q_INVOKABLE qint64 bytesOf(const QString& itemId) const;
    Q_INVOKABLE int prepare(const QString& itemId, bool unplayedOnly = false);
    // Queues what prepare() found and says so in a toast.
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
    Q_INVOKABLE bool remove(const QString& itemId);
    Q_INVOKABLE int removeWatched();
    Q_INVOKABLE int removeAll();
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    // Plays a downloaded track with the other downloaded tracks of its
    // album after it. False when there is no music player.
    Q_INVOKABLE bool playTrack(const QString& trackId);

  Q_SIGNALS:
    void changed();
    void progressChanged();
    void prepared(int requestId, const QVariantMap& summary);
    void prepareFailed(int requestId, const QString& text);
    // For the shell's toast.
    void message(const QString& text, bool error);

  private:
    void connectService();
    void rebuildRows();

    library::ItemListModel m_resume;
    library::ItemListModel m_movies;
    library::ItemListModel m_episodes;
    library::ItemListModel m_music;
    library::ItemListModel m_other;
    int m_rowsRevision = -1;

    inline static QPointer<offline::DownloadsController> s_service;
    inline static DownloadsUi* s_instance = nullptr;
    inline static TrackListHandler s_tracks;
  };

  struct DownloadListModelForeign {
    Q_GADGET
    QML_FOREIGN(jfd::offline::DownloadListModel)
    QML_NAMED_ELEMENT(DownloadListModel)
    QML_UNCREATABLE("Use Downloads.queue or Downloads.downloaded")
  };

} // namespace jfd::ui
