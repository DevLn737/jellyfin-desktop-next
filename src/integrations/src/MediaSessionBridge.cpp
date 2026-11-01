#include <jfd/integrations/MediaSessionBridge.h>

#include <jfd/integrations/DiscordPresence.h>
#include <jfd/platform/SleepInhibitor.h>

#include <QCoreApplication>

namespace jfd::integrations {

  namespace {
    using core::PlaybackPhase;
    using platform::MediaCommand;
    using Status = platform::MediaControlsState::Status;

    bool active(PlaybackPhase phase) {
      return phase != PlaybackPhase::Idle && phase != PlaybackPhase::Ended &&
             phase != PlaybackPhase::Failed;
    }

    // "Series · S1 E2" or "Series · S1 E2–3".
    QString episodeLine(const NowPlayingDetails& details) {
      QString line = details.seriesName;
      if (details.season && details.episode) {
        QString number = QStringLiteral("S%1 E%2").arg(*details.season).arg(*details.episode);
        if (details.episodeEnd && *details.episodeEnd > *details.episode)
          number += QStringLiteral("–%1").arg(*details.episodeEnd);
        line = line.isEmpty() ? number : line + QStringLiteral(" · ") + number;
      }
      return line;
    }
  } // namespace

  platform::MediaControlsState mediaControlsState(const core::SessionSnapshot& snapshot,
                                                  const NowPlayingDetails* details) {
    platform::MediaControlsState state;
    if (!active(snapshot.phase) || snapshot.title.empty()) return state;

    const bool playing =
        snapshot.phase == PlaybackPhase::Playing || snapshot.phase == PlaybackPhase::Buffering;
    state.status = playing ? Status::Playing : Status::Paused;
    state.title = QString::fromStdString(snapshot.title);
    state.positionMs = snapshot.position.milliseconds();
    state.durationMs = snapshot.duration ? snapshot.duration->milliseconds() : 0;
    state.rate = snapshot.effectiveRate.value();
    state.canSeek = state.durationMs > 0;
    const bool inGroup = snapshot.group.has_value();
    state.canGoNext = inGroup || (details && details->hasNext);
    state.canGoPrevious = inGroup || (details && details->hasPrevious);
    if (details) {
      state.artUrl = details->artUrl;
      if (details->kind == PresenceInfo::Kind::Episode)
        state.subtitle = episodeLine(*details);
      else if (details->kind == PresenceInfo::Kind::Music)
        state.subtitle = details->artist;
      else if (details->year > 0)
        state.subtitle = QString::number(details->year);
    }
    return state;
  }

  std::optional<PresenceInfo> presenceInfo(const core::SessionSnapshot& snapshot,
                                           const NowPlayingDetails* details) {
    if (!active(snapshot.phase) || snapshot.title.empty()) return std::nullopt;
    PresenceInfo info;
    info.title = QString::fromStdString(snapshot.title);
    info.positionMs = snapshot.position.milliseconds();
    info.durationMs = snapshot.duration ? snapshot.duration->milliseconds() : 0;
    info.playing = snapshot.phase == PlaybackPhase::Playing;
    info.buffering =
        snapshot.phase == PlaybackPhase::Buffering || snapshot.phase == PlaybackPhase::Loading;
    if (snapshot.group) info.groupSize = static_cast<int>(snapshot.group->participants.size());
    if (details) {
      info.kind = details->kind;
      info.seriesName = details->seriesName;
      info.artist = details->artist;
      info.album = details->album;
      info.season = details->season;
      info.episode = details->episode;
      info.episodeEnd = details->episodeEnd;
      info.year = details->year;
      info.poster = details->publicPoster;
    }
    return info;
  }

  bool keepsDisplayAwake(const core::SessionSnapshot& snapshot) {
    return snapshot.phase == PlaybackPhase::Playing || snapshot.phase == PlaybackPhase::Buffering;
  }

  std::optional<core::SessionInput> commandInput(MediaCommand command, PlaybackPhase phase) {
    switch (command) {
    case MediaCommand::Play: return core::intent::Play{};
    case MediaCommand::Pause: return core::intent::Pause{};
    case MediaCommand::PlayPause:
      if (!active(phase)) return std::nullopt;
      if (phase == PlaybackPhase::Paused) return core::intent::Play{};
      return core::intent::Pause{};
    case MediaCommand::Stop: return core::intent::Stop{};
    case MediaCommand::Next: return core::intent::Next{};
    case MediaCommand::Previous: return core::intent::Previous{};
    }
    return std::nullopt;
  }

  MediaSessionBridge::MediaSessionBridge(Post post, QObject* parent)
      : QObject(parent), m_post(std::move(post)) {}

  void MediaSessionBridge::addControls(platform::MediaControls* controls) {
    if (!controls) return;
    m_controls.emplace_back(controls);
    connect(controls, &platform::MediaControls::commandRequested, this, &MediaSessionBridge::command);
    connect(controls, &platform::MediaControls::seekRequested, this, &MediaSessionBridge::seek);
    const auto state = mediaControlsState(m_snapshot, currentDetails());
    controls->setState(state);
    m_lastControls = state;
  }

  void MediaSessionBridge::setPresence(DiscordPresence* presence) {
    m_presence = presence;
    if (m_presence) m_presence->update(presenceInfo(m_snapshot, currentDetails()));
  }

  void MediaSessionBridge::setSleepInhibitor(platform::SleepInhibitor* inhibitor) {
    m_inhibitor = inhibitor;
    m_lastAwake.reset();
    publish();
  }

  void MediaSessionBridge::setDetails(NowPlayingDetails details) {
    m_details = std::move(details);
    publish();
    if (m_presence) m_presence->update(presenceInfo(m_snapshot, currentDetails()));
  }

  void MediaSessionBridge::update(const core::SessionSnapshot& snapshot) {
    m_snapshot = snapshot;
    publish();
    if (m_presence) m_presence->update(presenceInfo(m_snapshot, currentDetails()));
  }

  const NowPlayingDetails* MediaSessionBridge::currentDetails() const {
    if (m_details && m_details->item == m_snapshot.item) return &*m_details;
    return nullptr;
  }

  void MediaSessionBridge::publish() {
    const auto state = mediaControlsState(m_snapshot, currentDetails());
    if (state != m_lastControls) {
      m_lastControls = state;
      for (const auto& controls : m_controls)
        if (controls) controls->setState(state);
    }
    const bool awake = keepsDisplayAwake(m_snapshot);
    if (m_inhibitor && awake != m_lastAwake) {
      m_lastAwake = awake;
      m_inhibitor->setInhibited(awake, QCoreApplication::translate("MediaSessionBridge", "Playing video"));
    }
  }

  void MediaSessionBridge::command(MediaCommand command) {
    if (auto input = commandInput(command, m_snapshot.phase)) m_post(std::move(*input));
  }

  void MediaSessionBridge::seek(qint64 positionMs) {
    if (!active(m_snapshot.phase)) return;
    if (auto position = core::MediaTicks::fromMilliseconds(positionMs))
      m_post(core::intent::Seek{*position});
  }

} // namespace jfd::integrations
