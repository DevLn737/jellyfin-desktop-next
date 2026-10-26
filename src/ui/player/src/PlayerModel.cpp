#include <jfd/ui/PlayerModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/LibraryImageProvider.h>

#include <MpvPlayer.h>
#include <MpvVideoItem.h>
#ifdef JFD_HAVE_D3D11_VIDEO
#include <MpvD3D11VideoItem.h>
#endif

#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantMap>

#include <algorithm>
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
    // Every file opens with the viewer's own tracks, in a group too.
    runtime.setTrackChooser([this](const api::Item& item, const api::MediaSource& source,
                                   session::PlaybackLauncher::Request& request) {
      if (item.mediaType == QLatin1String("Audio")) return;
      m_trackPreference.choose(source, request.audioStreamIndex, request.subtitleStreamIndex);
    });
    connect(&runtime.launcher(), &session::PlaybackLauncher::failed, this,
            [this](const QString&, const QString& message) { Q_EMIT failed(message); });
    // Next and Previous outside a group; in a group the server's queue moves.
    connect(&runtime, &session::SessionRuntime::queueStepRequested, this, &PlayerModel::step);
    m_subscription = runtime.hub().subscribe(
        [this](const core::SessionSnapshot& snapshot) { onSnapshot(snapshot); });
  }

  PlayerModel::~PlayerModel() {
    m_runtime.setTrackChooser({});
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

  void PlayerModel::setAutoNext(bool on) {
    if (on == m_autoNext) return;
    m_autoNext = on;
    Q_EMIT autoNextChanged();
  }

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
    if (!m_current) return;
    if (paused())
      m_runtime.post(core::intent::Play{});
    else
      m_runtime.post(core::intent::Pause{});
  }

  void PlayerModel::seek(double seconds) {
    if (!m_current) return;
    const double clamped = std::max(0.0, duration() > 0 ? std::min(seconds, duration()) : seconds);
    if (const auto position =
            core::MediaTicks::fromTicks(static_cast<std::int64_t>(clamped * TicksPerSecond)))
      m_runtime.post(core::intent::Seek{*position});
  }

  void PlayerModel::seekBy(double seconds) { seek(position() + seconds); }

  void PlayerModel::setRate(double rate) {
    if (!m_current) return;
    if (const auto value = core::PlaybackRate::base(rate)) m_runtime.post(core::intent::SetRate{*value});
  }

  void PlayerModel::setProfileOptions(const api::DeviceProfileOptions& options) {
    m_profileOptions = options;
    applyDeviceProfile();
  }

  void PlayerModel::applyDeviceProfile() {
    QJsonObject profile = api::buildDeviceProfile(m_profileOptions);
    if (m_maxBitrate > 0) {
      // The server's StreamBuilder reads the streaming limit for video and
      // the static one for direct play.
      profile.insert(QStringLiteral("MaxStreamingBitrate"), static_cast<double>(m_maxBitrate));
      profile.insert(QStringLiteral("MaxStaticBitrate"), static_cast<double>(m_maxBitrate));
    }
    m_runtime.setDeviceProfile(profile);
  }

  void PlayerModel::setMaxBitrate(qint64 bitsPerSecond) {
    bitsPerSecond = std::max<qint64>(0, bitsPerSecond);
    if (bitsPerSecond == m_maxBitrate) return;
    m_maxBitrate = bitsPerSecond;
    applyDeviceProfile();
    Q_EMIT maxBitrateChanged();
    if (m_current && m_epoch) {
      const auto subtitle = m_tracks.table().selected(TrackKind::Subtitle);
      reopen(playback::command::Reopen{m_tracks.table().selected(TrackKind::Audio), subtitle.value_or(-1)});
    }
  }

  QString PlayerModel::playMethod() const {
    return m_current ? api::playMethodName(m_current->stream.method) : QString();
  }

  void PlayerModel::setVolume(int volume) {
    volume = std::clamp(volume, 0, 100);
    if (volume == m_volume) return;
    m_volume = volume;
    if (m_player) m_player->setDoubleProperty("volume", volume);
    m_reporter.setVolume(m_volume, m_muted);
    Q_EMIT volumeChanged();
  }

  void PlayerModel::setMuted(bool muted) {
    if (muted == m_muted) return;
    m_muted = muted;
    if (m_player) m_player->setStringProperty("mute", muted ? QByteArrayLiteral("yes") : QByteArrayLiteral("no"));
    m_reporter.setVolume(m_volume, m_muted);
    Q_EMIT volumeChanged();
  }

  void PlayerModel::selectAudio(int streamIndex) { selectTrack(TrackKind::Audio, streamIndex); }

  void PlayerModel::selectSubtitle(int streamIndex) {
    selectTrack(TrackKind::Subtitle, streamIndex < 0 ? std::nullopt : std::optional<int>(streamIndex));
  }

  void PlayerModel::selectTrack(TrackKind kind, std::optional<int> streamIndex) {
    if (m_current) m_trackPreference.remember(kind, m_current->source, streamIndex);
    m_tracks.select(kind, streamIndex);
  }

  void PlayerModel::skipSegment() {
    if (m_current) m_skipper.skip();
  }

  void PlayerModel::next() {
    if (m_current) m_runtime.post(core::intent::Next{});
  }

  void PlayerModel::previous() {
    if (m_current) m_runtime.post(core::intent::Previous{});
  }

  // Stops the player's own file. Music (or anything else the session plays)
  // is not the video player's to stop.
  void PlayerModel::stop() {
    m_waitingPlay.reset();
    m_runtime.launcher().cancel();
    if (m_current) m_runtime.post(core::intent::Stop{});
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
    // Music has its own player: the session opens it, the video player
    // neither shows nor reports it. The launcher drops an older request in
    // its favour, so a video still waiting will not open.
    if (item.mediaType == QLatin1String("Audio")) {
      m_pending.reset();
      return;
    }
    m_pending = Pending{request, source, stream};
    loadEpisodes(item);
    loadTrickplay(request.itemId);
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
    const bool had = hasNext() || hasPrevious() || !m_episodes.isEmpty();
    m_nextId.clear();
    m_previousId.clear();
    m_episodes.clear();
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
      for (const api::Item& episode : episodes) {
        m_episodes.append(QVariantMap{{QStringLiteral("id"), episode.id},
                                      {QStringLiteral("name"), episode.name},
                                      {QStringLiteral("season"), episode.parentIndexNumber.value_or(0)},
                                      {QStringLiteral("number"), episode.indexNumber.value_or(0)},
                                      {QStringLiteral("played"), episode.userData.played},
                                      {QStringLiteral("current"), episode.id == itemId}});
      }
      for (qsizetype i = 0; i < episodes.size(); ++i) {
        if (episodes[i].id != itemId) continue;
        if (i > 0) m_previousId = episodes[i - 1].id;
        if (i + 1 < episodes.size()) m_nextId = episodes[i + 1].id;
        break;
      }
      Q_EMIT queueChanged();
    });
  }

  void PlayerModel::loadTrickplay(const QString& itemId) {
    if (itemId == m_trickplayItem) return;
    m_trickplayItem = itemId;
    m_trickplay.clear();
    // The launcher's item has no trickplay field; the detail request has.
    m_client.itemDetails(itemId, this, [this, itemId](api::Result<api::Item> item) {
      if (itemId != m_trickplayItem) return;
      if (!item) {
        qCInfo(lcPlayer) << "no trickplay info:" << item.error().message;
        return;
      }
      m_trickplay = item->trickplay;
      chooseTrickplay();
    });
  }

  void PlayerModel::chooseTrickplay() {
    std::optional<api::TrickplayInfo> chosen;
    if (m_current && m_current->request.itemId == m_trickplayItem) {
      // Sorted by width: the smallest that is still sharp at its size on
      // the seek bar, else the largest there is.
      const QList<api::TrickplayInfo> resolutions = m_trickplay.value(m_current->source.id);
      for (const api::TrickplayInfo& info : resolutions) {
        chosen = info;
        if (info.width >= 240) break;
      }
    }
    const bool had = m_trickplayInfo.has_value();
    m_trickplayInfo = chosen;
    if (had || chosen) Q_EMIT trickplayChanged();
  }

  QVariantMap PlayerModel::trickplayFrame(double seconds) const {
    if (!m_trickplayInfo || !m_current) return {};
    const api::TrickplayInfo& info = *m_trickplayInfo;
    const auto frame = api::trickplayFrame(info, static_cast<std::int64_t>(std::llround(seconds * 1000)));
    if (!frame) return {};
    const QUrl tile =
        m_client.trickplayTileUrl(m_current->request.itemId, m_current->source.id, info.width, frame->tileIndex);
    return {
        {QStringLiteral("source"), LibraryImageProvider::sourceFor(tile)},
        {QStringLiteral("x"), frame->x},
        {QStringLiteral("y"), frame->y},
        {QStringLiteral("width"), frame->width},
        {QStringLiteral("height"), frame->height},
        {QStringLiteral("tileWidth"), info.width * info.tileWidth},
        {QStringLiteral("tileHeight"), info.height * info.tileHeight},
    };
  }

  void PlayerModel::playEpisode(const QString& itemId) {
    if (itemId.isEmpty() || inGroup()) return;
    play(itemId, 0);
  }

  void PlayerModel::step(bool forward) {
    if (!m_current) return;
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
    chooseTrickplay();
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

  bool PlayerModel::belongsToPlayer(const core::SessionSnapshot& snapshot) const {
    // Idle: nothing is open. An ended session no longer names its file; it
    // is ours only if we were showing one.
    if (!snapshot.media.isValid()) return snapshot.phase == PlaybackPhase::Idle || m_current.has_value();
    if (m_epoch && snapshot.media == *m_epoch) return true;
    // A file this player asked for, opening now.
    return m_pending && snapshot.media != m_snapshot.media &&
           snapshot.item.value() == m_pending->request.itemId.toStdString();
  }

  void PlayerModel::releaseFile(const core::SessionSnapshot& snapshot) {
    // The reporter ends the replaced file's report (Stopped, at its last
    // position) when it sees another file.
    m_reporter.snapshotChanged(snapshot);
    m_epoch.reset();
    m_current.reset();
    const bool hadQueue = hasNext() || hasPrevious() || !m_episodes.isEmpty();
    m_nextId.clear();
    m_previousId.clear();
    m_episodes.clear();
    if (hadQueue) Q_EMIT queueChanged();
    // What stays true while someone else plays: the group, the personal rate.
    core::SessionSnapshot idle;
    idle.revision = snapshot.revision;
    idle.group = snapshot.group;
    idle.personalRate = snapshot.personalRate;
    core::SessionSnapshot before = m_snapshot;
    before.revision = idle.revision;
    if (before == idle) return;
    m_snapshot = idle;
    if (!m_segment.isEmpty()) {
      m_segment.clear();
      Q_EMIT segmentChanged();
    }
    Q_EMIT positionChanged();
    Q_EMIT stateChanged();
  }

  void PlayerModel::onSnapshot(const core::SessionSnapshot& snapshot) {
    if (!belongsToPlayer(snapshot)) {
      releaseFile(snapshot);
      return;
    }
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
    if (justEnded && m_autoNext && !inGroup()) step(true);
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
