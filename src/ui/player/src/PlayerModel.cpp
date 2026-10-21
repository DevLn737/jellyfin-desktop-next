#include <jfd/ui/PlayerModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/session/SessionRuntime.h>

#include <MpvPlayer.h>
#include <MpvVideoItem.h>
#ifdef JFD_HAVE_D3D11_VIDEO
#include <MpvD3D11VideoItem.h>
#endif

#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantMap>

#include <cmath>

namespace jfd::ui {

  namespace {
    Q_LOGGING_CATEGORY(lcPlayer, "jfd.player")

    using core::PlaybackPhase;
    using core::TrackKind;

    constexpr double TicksPerSecond = 1e7;

    // Fills the host and keeps following its size.
    template <class Video> Video* makeVideo(QQuickItem* host, media::MpvPlayer* player) {
      auto* video = new Video(host);
      video->setObjectName(QStringLiteral("video"));
      video->setPlayer(player);
      video->setSize(host->size());
      QObject::connect(host, &QQuickItem::widthChanged, video,
                       [video, host] { video->setWidth(host->width()); });
      QObject::connect(host, &QQuickItem::heightChanged, video,
                       [video, host] { video->setHeight(host->height()); });
      return video;
    }

    template <class Video> void watchVideo(Video* video, PlayerModel* model,
                                           std::function<void()> ready) {
      // Both signals come from the render thread.
      QObject::connect(video, &Video::renderContextReady, model,
                       [ready](const QString& renderer) {
                         qCInfo(lcPlayer) << "video renderer:" << renderer;
                         ready();
                       }, Qt::QueuedConnection);
      QObject::connect(video, &Video::renderError, model,
                       [model](const QString& message) {
                         qCWarning(lcPlayer) << "video renderer failed:" << message;
                         Q_EMIT model->failed(message);
                       }, Qt::QueuedConnection);
    }
  } // namespace

  PlayerModel::PlayerModel(session::SessionRuntime& runtime, api::JellyfinClient& client,
                           media::MpvPlayer* player, QObject* parent)
      : QObject(parent), m_runtime(runtime), m_client(client), m_player(player),
        m_sink(playback::clientReportSink(client)), m_reporter(*m_sink),
        m_tracks({
            [&runtime](core::SessionInput input) { runtime.post(std::move(input)); },
            [this](const playback::command::Reopen& command) { reopen(command); },
            [this](std::optional<int> audio, std::optional<int> subtitle) {
              m_reporter.setTracks(audio, subtitle);
              Q_EMIT tracksChanged();
            },
        }),
        m_skipper([&runtime](core::SessionInput input) { runtime.post(std::move(input)); }) {
    connect(&runtime.launcher(), &session::PlaybackLauncher::resolved, this, &PlayerModel::onResolved);
    connect(&runtime.launcher(), &session::PlaybackLauncher::failed, this,
            [this](const QString&, const QString& message) { Q_EMIT failed(message); });
    // Next and Previous outside a group; in a group the server's queue moves.
    connect(&runtime, &session::SessionRuntime::queueStepRequested, this, &PlayerModel::step);
    m_subscription = runtime.hub().subscribe(
        [this](const core::SessionSnapshot& snapshot) { onSnapshot(snapshot); });
  }

  PlayerModel::~PlayerModel() {
    m_subscription.reset();
    m_reporter.end();
  }

  bool PlayerModel::active() const {
    return m_snapshot.phase != PlaybackPhase::Idle || m_waitingPlay.has_value();
  }

  bool PlayerModel::paused() const { return m_snapshot.phase == PlaybackPhase::Paused; }

  bool PlayerModel::loading() const {
    return m_waitingPlay || m_snapshot.phase == PlaybackPhase::Loading ||
           m_snapshot.phase == PlaybackPhase::Buffering;
  }

  QString PlayerModel::error() const {
    return m_snapshot.phase == PlaybackPhase::Failed ? QString::fromStdString(m_snapshot.error)
                                                     : QString();
  }

  QString PlayerModel::title() const { return QString::fromStdString(m_snapshot.title); }

  double PlayerModel::duration() const {
    return m_snapshot.duration ? m_snapshot.duration->seconds() : 0.0;
  }

  QString PlayerModel::segment() const { return m_segment; }

  void PlayerModel::setAutoSkip(bool on) {
    if (on == m_skipper.autoSkip()) return;
    m_skipper.setAutoSkip(on);
    Q_EMIT autoSkipChanged();
  }

  void PlayerModel::setSubtitleDelay(double seconds) {
    seconds = std::round(seconds * 10) / 10;
    if (seconds == m_subtitleDelay) return;
    m_subtitleDelay = seconds;
    if (m_player) m_player->setDoubleProperty("sub-delay", seconds);
    Q_EMIT delaysChanged();
  }

  void PlayerModel::setAudioDelay(double seconds) {
    seconds = std::round(seconds * 10) / 10;
    if (seconds == m_audioDelay) return;
    m_audioDelay = seconds;
    if (m_player) m_player->setDoubleProperty("audio-delay", seconds);
    Q_EMIT delaysChanged();
  }

  void PlayerModel::play(const QString& itemId, qint64 startTicks) {
    // Without a player (tests) nothing renders, so nothing to wait for.
    if (!m_videoReady && m_player) {
      // mpv opens its video output only once the render context exists.
      m_waitingPlay = std::make_pair(itemId, startTicks);
      Q_EMIT stateChanged();
      return;
    }
    m_runtime.play(itemId, startTicks);
  }

  void PlayerModel::togglePause() {
    if (paused())
      m_runtime.post(core::intent::Play{});
    else
      m_runtime.post(core::intent::Pause{});
  }

  void PlayerModel::seek(double seconds) {
    const double clamped = std::max(0.0, duration() > 0 ? std::min(seconds, duration()) : seconds);
    if (const auto position =
            core::MediaTicks::fromTicks(static_cast<std::int64_t>(clamped * TicksPerSecond)))
      m_runtime.post(core::intent::Seek{*position});
  }

  void PlayerModel::seekBy(double seconds) { seek(position() + seconds); }

  void PlayerModel::selectAudio(int streamIndex) { m_tracks.select(TrackKind::Audio, streamIndex); }

  void PlayerModel::selectSubtitle(int streamIndex) {
    m_tracks.select(TrackKind::Subtitle,
                    streamIndex < 0 ? std::nullopt : std::optional<int>(streamIndex));
  }

  void PlayerModel::skipSegment() { m_skipper.skip(); }

  void PlayerModel::next() { m_runtime.post(core::intent::Next{}); }

  void PlayerModel::previous() { m_runtime.post(core::intent::Previous{}); }

  void PlayerModel::stop() {
    m_waitingPlay.reset();
    m_runtime.launcher().cancel();
    m_runtime.post(core::intent::Stop{});
    Q_EMIT stateChanged();
  }

  void PlayerModel::attachVideo(QQuickItem* host, QQuickItem* owner) {
    if (!host || !host->window()) return;
    const auto ready = [this] { videoReady(); };
    QQuickItem* video = nullptr;
#ifdef JFD_HAVE_D3D11_VIDEO
    if (host->window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11) {
      auto* item = makeVideo<media::MpvD3D11VideoItem>(host, m_player);
      watchVideo(item, this, ready);
      video = item;
    }
#endif
    if (!video) {
      auto* item = makeVideo<media::MpvVideoItem>(host, m_player);
      watchVideo(item, this, ready);
      video = item;
    }
    if (owner && owner != host) connect(owner, &QObject::destroyed, video, &QObject::deleteLater);
  }

  void PlayerModel::videoReady() {
    m_videoReady = true;
    if (auto waiting = std::exchange(m_waitingPlay, std::nullopt))
      m_runtime.play(waiting->first, waiting->second);
  }

  void PlayerModel::onResolved(const session::PlaybackLauncher::Request& request, const api::Item& item,
                               const api::MediaSource& source, const api::PlaybackStream& stream) {
    m_pending = Pending{request, source, stream};
    loadEpisodes(item);
    if (m_segmentsItem != request.itemId) {
      m_segmentsItem = request.itemId;
      m_segments.clear();
      const QString itemId = request.itemId;
      m_client.mediaSegments(itemId, this, [this, itemId](api::Result<QList<api::MediaSegment>> result) {
        if (itemId != m_segmentsItem) return;
        if (!result) {
          qCInfo(lcPlayer) << "no media segments:" << result.error().message;
          return;
        }
        m_segments = *result;
        if (m_epoch && m_current && m_current->request.itemId == itemId)
          m_skipper.begin(*m_epoch, m_segments);
      });
    }
  }

  void PlayerModel::loadEpisodes(const api::Item& item) {
    if (item.id == m_queueItem) return;
    m_queueItem = item.id;
    const bool had = hasNext() || hasPrevious();
    m_nextId.clear();
    m_previousId.clear();
    if (had) Q_EMIT queueChanged();
    if (item.type != QStringLiteral("Episode") || item.seriesId.isEmpty()) return;
    const QString itemId = item.id;
    // All of the series' episodes, in season and episode order.
    m_client.episodes(item.seriesId, QString(), this, [this, itemId](api::Result<api::ItemPage> page) {
      if (itemId != m_queueItem) return;
      if (!page) {
        qCInfo(lcPlayer) << "no episode list:" << page.error().message;
        return;
      }
      const QList<api::Item>& episodes = page->items;
      for (qsizetype i = 0; i < episodes.size(); ++i) {
        if (episodes[i].id != itemId) continue;
        if (i > 0) m_previousId = episodes[i - 1].id;
        if (i + 1 < episodes.size()) m_nextId = episodes[i + 1].id;
        break;
      }
      Q_EMIT queueChanged();
    });
  }

  void PlayerModel::step(bool forward) {
    const QString target = forward ? m_nextId : m_previousId;
    if (!target.isEmpty()) play(target, 0);
  }

  void PlayerModel::beginFile(core::MediaEpoch epoch) {
    m_epoch = epoch;
    m_current = std::exchange(m_pending, std::nullopt);
    const api::MediaSource& source = m_current->source;
    const auto& request = m_current->request;
    const std::optional<int> audio =
        request.audioStreamIndex ? request.audioStreamIndex : source.defaultAudioStreamIndex;
    const std::optional<int> subtitle = request.subtitleStreamIndex
                                            ? request.subtitleStreamIndex
                                            : source.defaultSubtitleStreamIndex;
    m_reporter.begin(epoch, m_current->stream, audio, subtitle);
    m_tracks.begin(epoch, source, m_current->stream, audio, subtitle);
    m_skipper.begin(epoch, m_segmentsItem == request.itemId ? m_segments
                                                            : QList<api::MediaSegment>{});
    // Delays belong to the file: a new one starts in sync.
    setSubtitleDelay(0);
    setAudioDelay(0);
    Q_EMIT tracksChanged();
  }

  void PlayerModel::reopen(const playback::command::Reopen& command) {
    if (!m_current) return;
    session::PlaybackLauncher::Request request = m_current->request;
    request.startTicks = m_snapshot.position.ticks();
    request.mediaSourceId = m_current->source.id;
    request.audioStreamIndex = command.audioStreamIndex;
    request.subtitleStreamIndex = command.subtitleStreamIndex;
    request.startPaused = paused();
    m_runtime.launcher().open(request);
  }

  void PlayerModel::onSnapshot(const core::SessionSnapshot& snapshot) {
    const bool positionOnly = [&] {
      core::SessionSnapshot copy = snapshot;
      copy.position = m_snapshot.position;
      copy.revision = m_snapshot.revision;
      return copy == m_snapshot;
    }();
    const bool newFile = snapshot.media != m_snapshot.media;
    const bool justEnded = snapshot.phase == PlaybackPhase::Ended &&
                           (newFile || m_snapshot.phase != PlaybackPhase::Ended);
    m_snapshot = snapshot;

    if (newFile && m_pending &&
        snapshot.item.value() == m_pending->request.itemId.toStdString())
      beginFile(snapshot.media);

    m_reporter.snapshotChanged(snapshot);
    m_tracks.snapshotChanged(snapshot);
    m_skipper.snapshotChanged(snapshot);

    const auto current = m_skipper.current();
    const QString segment = current ? current->type : QString();
    if (segment != m_segment) {
      m_segment = segment;
      Q_EMIT segmentChanged();
    }
    Q_EMIT positionChanged();
    if (!positionOnly) Q_EMIT stateChanged();

    // The next episode follows; a group's server moves its own queue. The
    // ended session no longer names its file: the one begun last ended.
    if (justEnded && !inGroup() && m_current) step(true);
  }

  QVariantList PlayerModel::tracks(TrackKind kind) const {
    QVariantList list;
    for (const playback::TrackInfo& track : m_tracks.table().tracks(kind)) {
      QVariantMap entry;
      entry.insert(QStringLiteral("index"), track.streamIndex);
      entry.insert(QStringLiteral("title"), track.title);
      entry.insert(QStringLiteral("language"), track.language);
      entry.insert(QStringLiteral("codec"), track.codec);
      entry.insert(QStringLiteral("isDefault"), track.isDefault);
      entry.insert(QStringLiteral("isForced"), track.isForced);
      entry.insert(QStringLiteral("isExternal"), track.isExternal);
      list.append(entry);
    }
    return list;
  }

} // namespace jfd::ui
