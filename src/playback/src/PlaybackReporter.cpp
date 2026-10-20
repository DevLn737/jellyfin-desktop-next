#include <jfd/playback/PlaybackReporter.h>

#include <jfd/api/JellyfinClient.h>

#include <QDebug>

#include <cstdlib>

namespace jfd::playback {

  namespace {
    using Phase = core::PlaybackPhase;

    constexpr std::int64_t TicksPerMillisecond = 10'000;
    // A position further than this from where playback should be is a seek.
    constexpr std::int64_t SeekThresholdTicks = 3'000 * TicksPerMillisecond;

    class ClientSink final : public QObject, public ReportSink {
    public:
      explicit ClientSink(api::JellyfinClient& client) : m_client(client) {}

      void started(const api::PlaybackProgress& progress) override {
        m_client.reportPlaybackStart(progress, this, logFailure("start"));
      }
      void progress(const api::PlaybackProgress& progress) override {
        m_client.reportPlaybackProgress(progress, this, logFailure("progress"));
      }
      void stopped(const api::PlaybackProgress& progress, bool failed) override {
        m_client.reportPlaybackStopped(progress, failed, this, logFailure("stopped"));
      }

    private:
      static api::Callback<api::Empty> logFailure(const char* what) {
        return [what](const api::Result<api::Empty>& result) {
          if (!result)
            qWarning().noquote() << "playback report" << what << "failed:" << result.error().message;
        };
      }

      api::JellyfinClient& m_client;
    };

    bool isActive(Phase phase) {
      return phase == Phase::Playing || phase == Phase::Paused || phase == Phase::Buffering;
    }
  } // namespace

  std::unique_ptr<ReportSink> clientReportSink(api::JellyfinClient& client) {
    return std::make_unique<ClientSink>(client);
  }

  PlaybackReporter::PlaybackReporter(ReportSink& sink, QObject* parent)
      : QObject(parent), m_sink(sink) {
    m_timer.setInterval(std::chrono::seconds(10));
    connect(&m_timer, &QTimer::timeout, this, [this] {
      if (m_phase == Phase::Playing) send(QStringLiteral("TimeUpdate"));
    });
  }

  PlaybackReporter::~PlaybackReporter() { end(); }

  void PlaybackReporter::setProgressInterval(std::chrono::milliseconds interval) {
    m_timer.setInterval(interval);
  }

  void PlaybackReporter::begin(core::MediaEpoch epoch, const api::PlaybackStream& stream,
                               std::optional<int> audioStreamIndex,
                               std::optional<int> subtitleStreamIndex) {
    end();
    m_epoch = epoch;
    m_started = false;
    m_ended = false;
    m_phase = Phase::Loading;
    const int volume = m_progress.volumeLevel;
    const bool muted = m_progress.isMuted;
    m_progress = {};
    m_progress.itemId = stream.itemId;
    m_progress.mediaSourceId = stream.mediaSourceId;
    m_progress.playSessionId = stream.playSessionId;
    m_progress.playMethod = stream.method;
    m_progress.audioStreamIndex = audioStreamIndex;
    m_progress.subtitleStreamIndex = subtitleStreamIndex;
    m_progress.volumeLevel = volume;
    m_progress.isMuted = muted;
  }

  void PlaybackReporter::end() { stop(false); }

  void PlaybackReporter::stop(bool failed) {
    m_timer.stop();
    if (m_started && !m_ended) {
      m_progress.eventName.clear();
      m_sink.stopped(m_progress, failed);
    }
    m_ended = true;
  }

  void PlaybackReporter::send(const QString& eventName) {
    if (!m_started || m_ended) return;
    m_progress.eventName = eventName;
    m_sink.progress(m_progress);
  }

  void PlaybackReporter::setTracks(std::optional<int> audioStreamIndex,
                                   std::optional<int> subtitleStreamIndex) {
    const bool audioChanged = audioStreamIndex != m_progress.audioStreamIndex;
    const bool subtitleChanged = subtitleStreamIndex != m_progress.subtitleStreamIndex;
    m_progress.audioStreamIndex = audioStreamIndex;
    m_progress.subtitleStreamIndex = subtitleStreamIndex;
    if (audioChanged) send(QStringLiteral("AudioTrackChange"));
    if (subtitleChanged) send(QStringLiteral("SubtitleTrackChange"));
  }

  void PlaybackReporter::setVolume(int level, bool muted) {
    if (level == m_progress.volumeLevel && muted == m_progress.isMuted) return;
    m_progress.volumeLevel = level;
    m_progress.isMuted = muted;
    send(QStringLiteral("VolumeChange"));
  }

  void PlaybackReporter::snapshotChanged(const core::SessionSnapshot& snapshot) {
    if (!m_epoch || snapshot.media != *m_epoch || m_ended) return;

    const std::int64_t position = snapshot.position.ticks();
    // Where playback should be by now if nothing but time had moved it.
    std::int64_t expected = m_progress.positionTicks;
    if (m_phase == Phase::Playing && m_sincePosition.isValid())
      expected += static_cast<std::int64_t>(static_cast<double>(m_sincePosition.elapsed()) *
                                            m_rate * TicksPerMillisecond);
    const bool seeked = m_started && std::llabs(position - expected) > SeekThresholdTicks;
    m_progress.positionTicks = position;
    m_sincePosition.start();

    // The rate the viewer chose (the group's), not a temporary correction.
    const core::PlaybackRate rate =
        snapshot.group ? snapshot.group->baseRate : snapshot.personalRate;
    const bool rateChanged = rate.value() != m_progress.playbackRate;
    m_progress.playbackRate = rate.value();
    m_rate = snapshot.effectiveRate.value();

    m_phase = snapshot.phase;
    switch (snapshot.phase) {
    case Phase::Failed:
      stop(true);
      return;
    case Phase::Ended:
    case Phase::Idle:
      stop(false);
      return;
    default:
      break;
    }
    if (!isActive(snapshot.phase)) return;

    // Buffering keeps the pause state it had.
    const bool wasPaused = m_progress.isPaused;
    if (snapshot.phase != Phase::Buffering) m_progress.isPaused = snapshot.phase == Phase::Paused;

    if (!m_started) {
      if (snapshot.phase == Phase::Buffering) return;
      m_started = true;
      m_progress.eventName.clear();
      m_sink.started(m_progress);
      m_timer.start();
      return;
    }
    if (wasPaused != m_progress.isPaused)
      send(m_progress.isPaused ? QStringLiteral("Pause") : QStringLiteral("Unpause"));
    else if (rateChanged)
      send(QStringLiteral("PlaybackRateChange"));
    else if (seeked)
      send(QStringLiteral("TimeUpdate"));
  }

} // namespace jfd::playback
