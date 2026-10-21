#pragma once

#include <jfd/api/Playback.h>
#include <jfd/core/SessionSnapshot.h>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <memory>
#include <optional>

namespace jfd::api {
  class JellyfinClient;
}

// Tells the server what is playing, from the session's snapshots:
// Sessions/Playing once the file plays, Sessions/Playing/Progress every 10 s
// while playing and at once on pause, resume, seek, rate and track changes,
// and Sessions/Playing/Stopped when the file ends, fails, or is replaced. The
// server marks the item played when Stopped arrives past its resume limit
// (90 % by default), so the final position matters and is always sent.
namespace jfd::playback {

  class ReportSink {
  public:
    virtual ~ReportSink() = default;
    virtual void started(const api::PlaybackProgress& progress) = 0;
    virtual void progress(const api::PlaybackProgress& progress) = 0;
    virtual void stopped(const api::PlaybackProgress& progress, bool failed) = 0;
  };

  // Sends reports with JellyfinClient; failures are only logged, since a lost
  // report must never disturb playback.
  std::unique_ptr<ReportSink> clientReportSink(api::JellyfinClient& client);

  class PlaybackReporter : public QObject, public core::SessionObserver {
    Q_OBJECT

  public:
    explicit PlaybackReporter(ReportSink& sink, QObject* parent = nullptr);
    ~PlaybackReporter() override;

    // The stream opened for a file, before the session reports it playing.
    // Ends the report of the previous file, if any.
    void begin(core::MediaEpoch epoch, const api::PlaybackStream& stream,
               std::optional<int> audioStreamIndex, std::optional<int> subtitleStreamIndex);
    // Sends Stopped for the current file now (the app is closing).
    void end();

    void setTracks(std::optional<int> audioStreamIndex, std::optional<int> subtitleStreamIndex);
    void setVolume(int level, bool muted);

    void setProgressInterval(std::chrono::milliseconds interval);

    void snapshotChanged(const core::SessionSnapshot& snapshot) override;
    void notify(const core::SessionNotice&) override {}

  private:
    void send(const QString& eventName);
    void stop(bool failed);

    ReportSink& m_sink;
    QTimer m_timer;
    QElapsedTimer m_sincePosition;

    std::optional<core::MediaEpoch> m_epoch;
    api::PlaybackProgress m_progress;
    bool m_started = false;
    bool m_ended = false;
    // A seek seen while loading, not yet sent: the next active snapshot sends it.
    bool m_seekPending = false;
    core::PlaybackPhase m_phase = core::PlaybackPhase::Idle;
    double m_rate = 1.0;
  };

} // namespace jfd::playback
