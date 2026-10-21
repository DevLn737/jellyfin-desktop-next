#pragma once

#include <jfd/api/Models.h>
#include <jfd/api/Playback.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/playback/PlaybackReporter.h>
#include <jfd/playback/SegmentSkipper.h>
#include <jfd/playback/TrackController.h>
#include <jfd/playback/TrackPreference.h>
#include <jfd/session/PlaybackLauncher.h>
#include <jfd/session/SessionHub.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>

class QQuickItem;

namespace jfd::api {
  class JellyfinClient;
}
namespace jfd::media {
  class MpvPlayer;
}
namespace jfd::session {
  class SessionRuntime;
}

namespace jfd::ui {

  // The player screen's state and commands, as the QML singleton
  // PlayerSession. Reads the playback session's snapshots; every command
  // is a session intent, so in a SyncPlay group pause, seek and skip act for
  // the whole group. Runs the track table, the media segments and the
  // playback reports for the file the session plays.
  class PlayerModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool ended READ ended NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(double rate READ rate NOTIFY stateChanged)
    Q_PROPERTY(bool inGroup READ inGroup NOTIFY stateChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    // Jellyfin stream indices; -1 when none (subtitles off).
    Q_PROPERTY(int audioIndex READ audioIndex NOTIFY tracksChanged)
    Q_PROPERTY(int subtitleIndex READ subtitleIndex NOTIFY tracksChanged)
    // The skippable segment playing now ("Intro", "Outro", ...), or empty.
    Q_PROPERTY(QString segment READ segment NOTIFY segmentChanged)
    Q_PROPERTY(bool autoSkip READ autoSkip WRITE setAutoSkip NOTIFY autoSkipChanged)
    // Seconds; positive delays subtitles or audio. Local to this client.
    Q_PROPERTY(double subtitleDelay READ subtitleDelay WRITE setSubtitleDelay NOTIFY delaysChanged)
    Q_PROPERTY(double audioDelay READ audioDelay WRITE setAudioDelay NOTIFY delaysChanged)
    // Outside a group: the series' next and previous episodes, if any.
    // At the end of an episode the next one plays.
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY queueChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY queueChanged)
    // The server has thumbnails for the seek bar (trickplayFrame).
    Q_PROPERTY(bool hasTrickplay READ hasTrickplay NOTIFY trickplayChanged)

  public:
    PlayerModel(session::SessionRuntime& runtime, api::JellyfinClient& client,
                media::MpvPlayer* player, QObject* parent = nullptr);
    ~PlayerModel() override;

    bool active() const;
    bool paused() const;
    bool loading() const;
    bool ended() const { return m_snapshot.phase == core::PlaybackPhase::Ended; }
    QString error() const;
    QString title() const;
    double position() const { return m_snapshot.position.seconds(); }
    double duration() const;
    double rate() const { return m_snapshot.effectiveRate.value(); }
    bool inGroup() const { return m_snapshot.group.has_value(); }
    QVariantList audioTracks() const { return tracks(core::TrackKind::Audio); }
    QVariantList subtitleTracks() const { return tracks(core::TrackKind::Subtitle); }
    int audioIndex() const { return m_tracks.table().selected(core::TrackKind::Audio).value_or(-1); }
    int subtitleIndex() const {
      return m_tracks.table().selected(core::TrackKind::Subtitle).value_or(-1);
    }
    QString segment() const;
    bool autoSkip() const { return m_skipper.autoSkip(); }
    void setAutoSkip(bool on);
    double subtitleDelay() const { return m_subtitleDelay; }
    void setSubtitleDelay(double seconds);
    double audioDelay() const { return m_audioDelay; }
    void setAudioDelay(double seconds);
    bool hasNext() const { return !m_nextId.isEmpty(); }
    bool hasPrevious() const { return !m_previousId.isEmpty(); }
    bool hasTrickplay() const { return m_trickplayInfo.has_value(); }

    // Plays an item alone. Waits for the video renderer when the page has
    // not drawn its first frame yet.
    Q_INVOKABLE void play(const QString& itemId, qint64 startTicks = 0);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekBy(double seconds);
    Q_INVOKABLE void selectAudio(int streamIndex);
    // -1 turns subtitles off.
    Q_INVOKABLE void selectSubtitle(int streamIndex);
    Q_INVOKABLE void skipSegment();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    // The seek bar thumbnail at `seconds`: {source, x, y, width, height,
    // tileWidth, tileHeight} in tile pixels, where source is the tile image
    // (image://jfd/...) and x, y, width, height the thumbnail inside it.
    // Empty without trickplay.
    Q_INVOKABLE QVariantMap trickplayFrame(double seconds) const;

    // Creates the video item for this platform's renderer inside `host`,
    // filling it. The page calls it once; the item is the host's child and
    // goes away with `owner` too, when given (a host outside the page).
    Q_INVOKABLE void attachVideo(QQuickItem* host, QQuickItem* owner = nullptr);

  Q_SIGNALS:
    void stateChanged();
    void positionChanged();
    void tracksChanged();
    void segmentChanged();
    void autoSkipChanged();
    void delaysChanged();
    void queueChanged();
    void trickplayChanged();
    // A play request could not be resolved, or the renderer failed.
    void failed(const QString& message);

  private:
    struct Pending {
      session::PlaybackLauncher::Request request;
      api::MediaSource source;
      api::PlaybackStream stream;
    };

    void onSnapshot(const core::SessionSnapshot& snapshot);
    void onResolved(const session::PlaybackLauncher::Request& request, const api::Item& item,
                    const api::MediaSource& source, const api::PlaybackStream& stream);
    void beginFile(core::MediaEpoch epoch);
    void reopen(const playback::command::Reopen& reopen);
    // A track the viewer picked: remembered for the next file.
    void selectTrack(core::TrackKind kind, std::optional<int> streamIndex);
    void videoReady();
    void loadEpisodes(const api::Item& item);
    void step(bool forward);
    void loadTrickplay(const QString& itemId);
    void chooseTrickplay();
    QVariantList tracks(core::TrackKind kind) const;

    session::SessionRuntime& m_runtime;
    api::JellyfinClient& m_client;
    QPointer<media::MpvPlayer> m_player;
    std::unique_ptr<playback::ReportSink> m_sink;
    playback::PlaybackReporter m_reporter;
    playback::TrackController m_tracks;
    playback::SegmentSkipper m_skipper;
    // The viewer's last audio and subtitle pick, for the next file.
    playback::TrackPreference m_trackPreference;
    session::SessionHub::Subscription m_subscription;

    core::SessionSnapshot m_snapshot;
    std::optional<core::MediaEpoch> m_epoch;
    // The latest resolved stream, waiting for the session to open it.
    std::optional<Pending> m_pending;
    // What plays now, for reopening a transcode with other tracks.
    std::optional<Pending> m_current;
    QList<api::MediaSegment> m_segments;
    QString m_segmentsItem;
    QString m_segment;
    double m_subtitleDelay = 0;
    double m_audioDelay = 0;
    // The episode the queue was built for, and its neighbours.
    QString m_queueItem;
    QString m_nextId;
    QString m_previousId;
    // Trickplay resolutions of the item (by media source id), and the one
    // the seek bar uses for the file playing now.
    QString m_trickplayItem;
    QHash<QString, QList<api::TrickplayInfo>> m_trickplay;
    std::optional<api::TrickplayInfo> m_trickplayInfo;
    bool m_videoReady = false;
    std::optional<std::pair<QString, qint64>> m_waitingPlay;
  };

} // namespace jfd::ui
