#pragma once

#include <jfd/api/MusicApi.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/music/MusicQueue.h>
#include <jfd/music/MusicQueueModel.h>
#include <jfd/playback/PlaybackReporter.h>
#include <jfd/session/PlaybackLauncher.h>
#include <jfd/session/SessionHub.h>

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <memory>
#include <optional>

namespace jfd::api {
  class JellyfinClient;
}
namespace jfd::session {
  class SessionRuntime;
}

namespace jfd::music {

  // The music player, as the QML singleton MusicSession: the queue and the
  // session's music file. The session plays one file at a time, video or
  // music; this plays the queue's tracks through it, reports each to the
  // server on a report of its own, and lets go when somebody else's file
  // (a video) takes the session: the queue stays, the track's report ends
  // with its position. The next track is queued behind the current one
  // (intent::PreloadNext) as soon as it plays, so the backend moves on
  // without a gap.
  class MusicPlayer : public QObject {
    Q_OBJECT
    // Music cannot play in a SyncPlay group.
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    // A music file is open: loading, playing or paused.
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    // A video (or any file that is not ours) plays; starting a track would
    // replace it, so the UI asks first.
    Q_PROPERTY(bool videoActive READ videoActive NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap currentTrack READ currentTrack NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString repeat READ repeat WRITE setRepeat NOTIFY modeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modeChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY queueChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY queueChanged)
    Q_PROPERTY(int queueSize READ queueSize NOTIFY queueChanged)
    Q_PROPERTY(jfd::music::MusicQueueModel* queue READ queueModel CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

  public:
    MusicPlayer(session::SessionRuntime& runtime, api::JellyfinClient& client, QObject* parent = nullptr);
    ~MusicPlayer() override;

    bool available() const { return !m_snapshot.group.has_value(); }
    bool active() const;
    bool playing() const { return active() && m_snapshot.phase == core::PlaybackPhase::Playing; }
    bool paused() const { return active() && m_snapshot.phase == core::PlaybackPhase::Paused; }
    bool loading() const;
    bool videoActive() const { return m_foreignActive; }
    double position() const { return m_epoch ? m_snapshot.position.seconds() : 0.0; }
    double duration() const;
    QVariantMap currentTrack() const;
    int currentIndex() const { return m_queue.current(); }
    QString repeat() const;
    bool shuffle() const { return m_queue.shuffle(); }
    bool hasNext() const { return m_queue.nextIndex(false).has_value(); }
    bool hasPrevious() const { return m_queue.previousIndex().has_value(); }
    int queueSize() const { return m_queue.size(); }
    MusicQueueModel* queueModel() { return &m_model; }
    const MusicQueue& queueState() const { return m_queue; }
    QString error() const { return m_error; }
    // How many tracks the backend moved on to by itself (queued behind the
    // one before), and how many were opened one by one; for diagnostics.
    // The next track is queued behind the current one.
    bool nextQueued() const { return m_preloaded.has_value(); }
    int handoverCount() const { return m_handovers; }
    int openCount() const { return m_opens; }

    void setRepeat(const QString& mode);
    void setShuffle(bool on);

    // Plays `tracks` from `start`, replacing the queue.
    void play(QList<Track> tracks, int start);
    // At the end of the queue / right after the current track.
    void append(QList<Track> tracks);
    void playNext(QList<Track> tracks);

    // Fetch an album's, a playlist's or an artist's tracks and play (or
    // queue) them.
    Q_INVOKABLE void playAlbum(const QString& albumId, int startIndex = 0, bool shuffle = false);
    Q_INVOKABLE void playPlaylist(const QString& playlistId, int startIndex = 0, bool shuffle = false);
    Q_INVOKABLE void playArtist(const QString& artistId, bool shuffle = true);
    Q_INVOKABLE void queueAlbum(const QString& albumId, bool next);

    // Plays the queue's track at `index`.
    Q_INVOKABLE void jumpTo(int index);
    // Picks up the saved queue where it stopped.
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void unpause();
    // Stops playing; the queue stays.
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    // After the first seconds of a track this restarts it.
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();

    // The queue and the position as saved in the profile; restoreState
    // ignores a state saved for another server or user.
    QByteArray saveState() const;
    void restoreState(const QByteArray& state);
    // Called a moment after the queue or the track changed.
    void setPersistence(std::function<void(const QByteArray&)> save) { m_save = std::move(save); }

  Q_SIGNALS:
    void stateChanged();
    void positionChanged();
    void currentChanged();
    void queueChanged();
    void modeChanged();
    void failed(const QString& message);

  private:
    struct Resolved {
      std::int64_t entryId = 0;
      session::PlaybackLauncher::Request request;
      api::PlaybackStream stream;
    };

    bool belongsToPlayer(const core::SessionSnapshot& snapshot) const;
    void onSnapshot(const core::SessionSnapshot& snapshot);
    void releaseFile(const core::SessionSnapshot& snapshot);
    void beginFile(core::MediaEpoch epoch, const Resolved& resolved);
    void onResolved(const session::PlaybackLauncher::Request& request, const api::Item& item,
                    const api::PlaybackStream& stream);
    void onPreloaded(const session::PlaybackLauncher::Request& request, const api::Item& item,
                     const api::PlaybackStream& stream);
    void step(bool forward);
    void open(int index, std::int64_t startTicks = 0);
    void preload();
    void queueEdited(bool nextMayHaveChanged);
    void currentMoved(int before);
    void fetchAndPlay(const QString& what, std::function<void(api::MusicApi&, QObject*, api::Callback<api::ItemPage>)> fetch,
                      int startIndex, bool shuffle);
    void changed();

    session::SessionRuntime& m_runtime;
    api::JellyfinClient& m_client;
    api::MusicApi m_api;
    std::unique_ptr<playback::ReportSink> m_sink;
    playback::PlaybackReporter m_reporter;
    session::SessionHub::Subscription m_subscription;
    QTimer m_saveTimer;
    std::function<void(const QByteArray&)> m_save;

    MusicQueue m_queue;
    MusicQueueModel m_model;
    core::SessionSnapshot m_snapshot;
    std::optional<core::MediaEpoch> m_epoch;
    // The track asked for and not yet resolved.
    std::optional<std::int64_t> m_requested;
    std::optional<Resolved> m_pending;
    // Queued behind the current file.
    std::optional<Resolved> m_preloaded;
    // The entry a preload was asked for (the answer may come later).
    std::optional<std::int64_t> m_preloadAsked;
    bool m_foreignActive = false;
    // The saved position the next resume() starts at.
    std::int64_t m_resumeTicks = 0;
    QString m_error;
    int m_handovers = 0;
    int m_opens = 0;
  };

} // namespace jfd::music
