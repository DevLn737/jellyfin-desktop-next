#include <jfd/music/MusicPlayer.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/session/SessionRuntime.h>

#include <QJsonDocument>

#include <algorithm>

namespace jfd::music {

  using core::PlaybackPhase;

  namespace {
    constexpr double TicksPerSecond = 1e7;
    // Past this many seconds, Previous restarts the track.
    constexpr double RestartAfterSeconds = 3.0;
    constexpr int SaveDelayMs = 1000;

    bool isOpen(PlaybackPhase phase) { return phase != PlaybackPhase::Idle && phase != PlaybackPhase::Ended; }

    QString ownerOf(const api::JellyfinClient& client) {
      return client.serverUrl().toString() + QLatin1Char('|') + client.userId();
    }
  } // namespace

  MusicPlayer::MusicPlayer(session::SessionRuntime& runtime, api::JellyfinClient& client, QObject* parent)
      : QObject(parent), m_runtime(runtime), m_client(client), m_api(client),
        m_sink(playback::clientReportSink(client)), m_reporter(*m_sink) {
    m_model.reset(&m_queue);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(SaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
      if (m_save) m_save(saveState());
    });
    auto& launcher = runtime.launcher();
    connect(&launcher, &session::PlaybackLauncher::resolved, this,
            [this](const session::PlaybackLauncher::Request& request, const api::Item& item,
                   const api::MediaSource&, const api::PlaybackStream& stream) { onResolved(request, item, stream); });
    connect(&launcher, &session::PlaybackLauncher::preloaded, this,
            [this](const session::PlaybackLauncher::Request& request, const api::Item& item,
                   const api::MediaSource&, const api::PlaybackStream& stream) { onPreloaded(request, item, stream); });
    connect(&launcher, &session::PlaybackLauncher::failed, this, [this](const QString& itemId, const QString&) {
      const Track* track = m_queue.currentTrack();
      if (!m_requested || !track || track->id != itemId) return;
      m_requested.reset();
      m_error = tr("This track could not be played.");
      Q_EMIT failed(m_error);
      Q_EMIT stateChanged();
    });
    connect(&runtime, &session::SessionRuntime::queueStepRequested, this, &MusicPlayer::step);
    m_subscription = runtime.hub().subscribe([this](const core::SessionSnapshot& s) { onSnapshot(s); });
  }

  MusicPlayer::~MusicPlayer() {
    m_subscription.reset();
    m_reporter.end();
    if (m_saveTimer.isActive() && m_save) m_save(saveState());
  }

  bool MusicPlayer::active() const { return m_epoch.has_value() && isOpen(m_snapshot.phase); }

  bool MusicPlayer::loading() const {
    if (m_requested || m_pending) return true;
    return active() && (m_snapshot.phase == PlaybackPhase::Loading || m_snapshot.phase == PlaybackPhase::Buffering);
  }

  double MusicPlayer::duration() const {
    if (m_epoch && m_snapshot.duration) return m_snapshot.duration->seconds();
    const Track* track = m_queue.currentTrack();
    return track ? static_cast<double>(track->runTimeTicks) / TicksPerSecond : 0.0;
  }

  QVariantMap MusicPlayer::currentTrack() const {
    const Track* track = m_queue.currentTrack();
    if (!track) return {};
    return {{QStringLiteral("id"), track->id},
            {QStringLiteral("title"), track->name},
            {QStringLiteral("artist"), track->artist},
            {QStringLiteral("album"), track->album},
            {QStringLiteral("albumId"), track->albumId},
            {QStringLiteral("imageItemId"), track->imageItemId},
            {QStringLiteral("imageTag"), track->imageTag},
            {QStringLiteral("duration"), static_cast<double>(track->runTimeTicks) / TicksPerSecond}};
  }

  QString MusicPlayer::repeat() const {
    switch (m_queue.repeat()) {
    case Repeat::None: return QStringLiteral("none");
    case Repeat::Queue: return QStringLiteral("queue");
    case Repeat::Track: return QStringLiteral("track");
    }
    return QStringLiteral("none");
  }

  void MusicPlayer::setRepeat(const QString& mode) {
    const Repeat repeat = mode == QLatin1String("queue")   ? Repeat::Queue
                          : mode == QLatin1String("track") ? Repeat::Track
                                                           : Repeat::None;
    if (repeat == m_queue.repeat()) return;
    m_queue.setRepeat(repeat);
    Q_EMIT modeChanged();
    queueEdited(true);
  }

  void MusicPlayer::setShuffle(bool on) {
    if (on == m_queue.shuffle()) return;
    m_queue.setShuffle(on);
    m_model.reset(&m_queue);
    Q_EMIT modeChanged();
    Q_EMIT currentChanged();
    queueEdited(true);
  }

  // --- The queue ---------------------------------------------------------------

  void MusicPlayer::play(QList<Track> tracks, int start) {
    if (tracks.isEmpty() || !available()) return;
    m_queue.replace(std::move(tracks), start);
    m_model.reset(&m_queue);
    Q_EMIT queueChanged();
    open(m_queue.current());
  }

  void MusicPlayer::append(QList<Track> tracks) {
    if (tracks.isEmpty()) return;
    m_queue.append(std::move(tracks));
    m_model.reset(&m_queue);
    queueEdited(true);
  }

  void MusicPlayer::playNext(QList<Track> tracks) {
    if (tracks.isEmpty()) return;
    m_queue.playNext(std::move(tracks));
    m_model.reset(&m_queue);
    queueEdited(true);
  }

  void MusicPlayer::removeAt(int index) {
    const bool wasCurrent = index == m_queue.current();
    const bool wasOpen = wasCurrent && m_epoch.has_value();
    if (m_queue.remove(index) == MusicQueue::Removed::Current && wasCurrent) {
      m_model.reset(&m_queue);
      Q_EMIT currentChanged();
      queueEdited(true);
      if (wasOpen) {
        if (m_queue.currentTrack()) open(m_queue.current());
        else stop();
      }
      return;
    }
    m_model.reset(&m_queue);
    Q_EMIT currentChanged();
    queueEdited(true);
  }

  void MusicPlayer::move(int from, int to) {
    m_queue.move(from, to);
    m_model.reset(&m_queue);
    Q_EMIT currentChanged();
    queueEdited(true);
  }

  void MusicPlayer::clear() {
    stop();
    m_queue.clear();
    m_model.reset(&m_queue);
    Q_EMIT currentChanged();
    queueEdited(true);
  }

  void MusicPlayer::currentMoved(int before) {
    m_model.currentChanged(before, m_queue.current());
    Q_EMIT currentChanged();
    Q_EMIT queueChanged();
    changed();
  }

  // After the queue or the mode changed: what follows the current track may
  // be another now, and the track queued behind the file with it.
  void MusicPlayer::queueEdited(bool nextMayHaveChanged) {
    Q_EMIT queueChanged();
    changed();
    if (!nextMayHaveChanged || !m_preloadAsked) return;
    const auto next = m_queue.nextIndex(true);
    const bool same = next && m_queue.repeat() != Repeat::Track && m_queue.tracks()[*next].entryId == *m_preloadAsked;
    if (same) return;
    m_preloaded.reset();
    m_preloadAsked.reset();
    preload();
  }

  // --- Playing -----------------------------------------------------------------

  void MusicPlayer::open(int index, std::int64_t startTicks) {
    if (index < 0 || index >= m_queue.size()) return;
    if (!available()) return;
    const int before = m_queue.current();
    m_queue.setCurrent(index);
    const Track& track = m_queue.tracks()[index];
    m_error.clear();
    m_requested = track.entryId;
    ++m_opens;
    m_pending.reset();
    m_preloaded.reset();
    m_preloadAsked.reset();
    session::PlaybackLauncher::Request request;
    request.itemId = track.id;
    request.startTicks = startTicks;
    m_runtime.launcher().open(request);
    currentMoved(before);
    Q_EMIT stateChanged();
  }

  void MusicPlayer::jumpTo(int index) { open(index); }

  void MusicPlayer::resume() {
    if (m_epoch && paused()) {
      unpause();
      return;
    }
    if (!m_queue.currentTrack()) {
      if (m_queue.isEmpty()) return;
      m_queue.setCurrent(0);
    }
    const std::int64_t ticks = std::exchange(m_resumeTicks, 0);
    open(m_queue.current(), ticks);
  }

  void MusicPlayer::togglePause() {
    if (paused()) unpause();
    else if (playing()) pause();
  }

  void MusicPlayer::pause() {
    if (m_epoch) m_runtime.post(core::intent::Pause{});
  }

  void MusicPlayer::unpause() {
    if (m_epoch) m_runtime.post(core::intent::Play{});
  }

  void MusicPlayer::stop() {
    if (m_requested || m_pending) m_runtime.launcher().cancel();
    m_requested.reset();
    m_pending.reset();
    m_preloaded.reset();
    m_preloadAsked.reset();
    if (m_epoch) m_runtime.post(core::intent::Stop{});
    Q_EMIT stateChanged();
  }

  void MusicPlayer::step(bool forward) {
    // Media keys reach every player; only the one with a file open acts.
    if (!m_epoch) return;
    if (forward) next();
    else previous();
  }

  void MusicPlayer::next() {
    if (const auto index = m_queue.nextIndex(false)) open(*index);
    else stop();
  }

  void MusicPlayer::previous() {
    if (position() > RestartAfterSeconds) {
      seek(0);
      return;
    }
    if (const auto index = m_queue.previousIndex()) open(*index);
    else seek(0);
  }

  void MusicPlayer::seek(double seconds) {
    if (!m_epoch) return;
    const double clamped = std::max(0.0, duration() > 0 ? std::min(seconds, duration()) : seconds);
    if (const auto position = core::MediaTicks::fromTicks(static_cast<std::int64_t>(clamped * TicksPerSecond)))
      m_runtime.post(core::intent::Seek{*position});
  }

  void MusicPlayer::preload() {
    if (!m_epoch || m_snapshot.group || m_preloadAsked || !isOpen(m_snapshot.phase)) return;
    if (m_queue.repeat() == Repeat::Track) return;
    const auto index = m_queue.nextIndex(true);
    if (!index) return;
    const Track& next = m_queue.tracks()[*index];
    const Track* now = m_queue.currentTrack();
    // One track queue on repeat: nothing different to queue.
    if (!now || next.entryId == now->entryId) return;
    m_preloadAsked = next.entryId;
    session::PlaybackLauncher::Request request;
    request.itemId = next.id;
    m_runtime.launcher().preloadNext(request);
  }

  // --- Following the session ---------------------------------------------------

  void MusicPlayer::onResolved(const session::PlaybackLauncher::Request& request, const api::Item& item,
                               const api::PlaybackStream& stream) {
    // A video took over the launcher: whatever we asked for will not open.
    if (item.mediaType != QLatin1String("Audio")) {
      m_requested.reset();
      m_pending.reset();
      return;
    }
    const Track* track = m_queue.currentTrack();
    if (!m_requested || !track || track->entryId != *m_requested || track->id != request.itemId) return;
    m_pending = Resolved{*m_requested, request, stream};
    m_requested.reset();
  }

  void MusicPlayer::onPreloaded(const session::PlaybackLauncher::Request& request, const api::Item& item,
                                const api::PlaybackStream& stream) {
    if (item.mediaType != QLatin1String("Audio") || !m_preloadAsked) return;
    const int index = m_queue.indexOfEntry(*m_preloadAsked);
    if (index < 0 || m_queue.tracks()[index].id != request.itemId) return;
    m_preloaded = Resolved{*m_preloadAsked, request, stream};
  }

  bool MusicPlayer::belongsToPlayer(const core::SessionSnapshot& snapshot) const {
    // Idle; or ended, which no longer names the file: ours if we had one.
    if (!snapshot.media.isValid()) return snapshot.phase == PlaybackPhase::Idle || m_epoch.has_value();
    if (m_epoch && snapshot.media == *m_epoch) return true;
    const std::string item = snapshot.item.value();
    if (m_pending && item == m_pending->request.itemId.toStdString()) return true;
    return m_epoch && m_preloaded && item == m_preloaded->request.itemId.toStdString();
  }

  void MusicPlayer::beginFile(core::MediaEpoch epoch, const Resolved& resolved) {
    m_epoch = epoch;
    m_preloadAsked.reset();
    m_reporter.begin(epoch, resolved.stream, std::nullopt, std::nullopt);
    changed();
  }

  void MusicPlayer::releaseFile(const core::SessionSnapshot& snapshot) {
    // The reporter ends the report of the file that was replaced, at its
    // last position.
    m_reporter.snapshotChanged(snapshot);
    const bool hadFile = m_epoch.has_value();
    m_epoch.reset();
    m_preloaded.reset();
    m_preloadAsked.reset();
    core::SessionSnapshot idle;
    idle.revision = snapshot.revision;
    idle.group = snapshot.group;
    idle.personalRate = snapshot.personalRate;
    m_snapshot = idle;
    if (hadFile) {
      // Where the track stopped, for resume().
      changed();
    }
    Q_EMIT positionChanged();
    Q_EMIT stateChanged();
  }

  void MusicPlayer::onSnapshot(const core::SessionSnapshot& snapshot) {
    if (!belongsToPlayer(snapshot)) {
      m_foreignActive = isOpen(snapshot.phase);
      // An open of ours still waiting loses to what the session plays now.
      if (m_foreignActive) {
        m_requested.reset();
        m_pending.reset();
      }
      releaseFile(snapshot);
      return;
    }
    m_foreignActive = false;

    const bool newFile = snapshot.media.isValid() && (!m_epoch || snapshot.media != *m_epoch);
    if (newFile) {
      const std::string item = snapshot.item.value();
      if (m_pending && item == m_pending->request.itemId.toStdString()) {
        beginFile(snapshot.media, *m_pending);
        m_pending.reset();
      } else if (m_preloaded && item == m_preloaded->request.itemId.toStdString()) {
        // The backend went on to the queued track by itself.
        const Resolved advanced = *m_preloaded;
        m_preloaded.reset();
        const int index = m_queue.indexOfEntry(advanced.entryId);
        if (index < 0) {
          // Taken out of the queue while it waited.
          m_runtime.post(core::intent::Stop{});
          releaseFile(snapshot);
          return;
        }
        const int before = m_queue.current();
        m_queue.setCurrent(index);
        ++m_handovers;
        beginFile(snapshot.media, advanced);
        currentMoved(before);
      }
    }

    const bool justEnded = snapshot.phase == PlaybackPhase::Ended && m_snapshot.phase != PlaybackPhase::Ended;
    const bool positionOnly = [&] {
      core::SessionSnapshot copy = snapshot;
      copy.position = m_snapshot.position;
      copy.revision = m_snapshot.revision;
      return copy == m_snapshot;
    }();
    m_snapshot = snapshot;
    m_reporter.snapshotChanged(snapshot);

    Q_EMIT positionChanged();
    if (!positionOnly) Q_EMIT stateChanged();

    if (justEnded && m_preloaded) {
      // Older mpv reports the end of the file before it starts the queued one.
      // Give the handover a moment before opening the next track ourselves.
      const auto entry = m_preloaded->entryId;
      QTimer::singleShot(600, this, [this, entry] {
        if (!m_preloaded || m_preloaded->entryId != entry) return;
        if (m_snapshot.phase != PlaybackPhase::Ended) return;
        m_preloaded.reset();
        m_epoch.reset();
        if (const auto index = m_queue.nextIndex(true)) open(*index);
      });
      return;
    }
    if (justEnded) {
      m_epoch.reset();
      if (const auto index = m_queue.nextIndex(true)) {
        open(*index);
      } else {
        // The queue played out.
        core::SessionSnapshot idle;
        idle.revision = snapshot.revision;
        idle.group = snapshot.group;
        idle.personalRate = snapshot.personalRate;
        m_snapshot = idle;
        m_resumeTicks = 0;
        changed();
        Q_EMIT stateChanged();
      }
      return;
    }
    if (m_pending == std::nullopt) preload();
  }

  // --- Albums, playlists, artists ----------------------------------------------

  void MusicPlayer::fetchAndPlay(const QString&,
                                 std::function<void(api::MusicApi&, QObject*, api::Callback<api::ItemPage>)> fetch,
                                 int startIndex, bool shuffle) {
    if (!available()) return;
    fetch(m_api, this, [this, startIndex, shuffle](api::Result<api::ItemPage> page) {
      if (!page) {
        m_error = page.error().message;
        Q_EMIT failed(m_error);
        return;
      }
      QList<Track> tracks;
      for (const api::Item& item : page->items)
        if (item.mediaType == QLatin1String("Audio")) tracks.append(Track::fromItem(item));
      m_queue.setShuffle(shuffle);
      Q_EMIT modeChanged();
      play(std::move(tracks), startIndex);
    });
  }

  void MusicPlayer::playAlbum(const QString& albumId, int startIndex, bool shuffle) {
    fetchAndPlay(albumId,
                 [albumId](api::MusicApi& api, QObject* context, api::Callback<api::ItemPage> done) {
                   api.albumTracks(albumId, context, std::move(done));
                 },
                 startIndex, shuffle);
  }

  void MusicPlayer::playPlaylist(const QString& playlistId, int startIndex, bool shuffle) {
    fetchAndPlay(playlistId,
                 [playlistId](api::MusicApi& api, QObject* context, api::Callback<api::ItemPage> done) {
                   api.playlistTracks(playlistId, context, std::move(done));
                 },
                 startIndex, shuffle);
  }

  void MusicPlayer::playArtist(const QString& artistId, bool shuffle) {
    fetchAndPlay(artistId,
                 [artistId](api::MusicApi& api, QObject* context, api::Callback<api::ItemPage> done) {
                   api::MusicQuery query;
                   query.artistIds = {artistId};
                   query.limit = 500;
                   api.songs(query, context, std::move(done));
                 },
                 0, shuffle);
  }

  void MusicPlayer::queueAlbum(const QString& albumId, bool next) {
    m_api.albumTracks(albumId, this, [this, next](api::Result<api::ItemPage> page) {
      if (!page) return;
      QList<Track> tracks;
      for (const api::Item& item : page->items)
        if (item.mediaType == QLatin1String("Audio")) tracks.append(Track::fromItem(item));
      if (next) playNext(std::move(tracks));
      else append(std::move(tracks));
    });
  }

  // --- Saved queue -------------------------------------------------------------

  void MusicPlayer::changed() { m_saveTimer.start(); }

  QByteArray MusicPlayer::saveState() const {
    const double seconds = m_epoch ? m_snapshot.position.seconds() : static_cast<double>(m_resumeTicks) / TicksPerSecond;
    const QJsonObject state{{QStringLiteral("version"), 1},
                            {QStringLiteral("owner"), ownerOf(m_client)},
                            {QStringLiteral("position"), seconds},
                            {QStringLiteral("queue"), m_queue.toJson()}};
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
  }

  void MusicPlayer::restoreState(const QByteArray& state) {
    const QJsonObject o = QJsonDocument::fromJson(state).object();
    // Another server or user: the saved tracks mean nothing here.
    if (o.value(QLatin1String("owner")).toString() != ownerOf(m_client)) return;
    auto queue = MusicQueue::fromJson(o.value(QLatin1String("queue")).toObject());
    if (!queue) return;
    m_queue = std::move(*queue);
    m_resumeTicks = static_cast<std::int64_t>(o.value(QLatin1String("position")).toDouble() * TicksPerSecond);
    m_model.reset(&m_queue);
    Q_EMIT queueChanged();
    Q_EMIT currentChanged();
    Q_EMIT modeChanged();
  }

} // namespace jfd::music
