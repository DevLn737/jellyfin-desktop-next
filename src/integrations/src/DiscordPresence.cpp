#include <jfd/integrations/DiscordPresence.h>

#include <QCoreApplication>

namespace jfd::integrations {

  namespace {
    // A position this far from the expected one is a seek.
    constexpr qint64 SeekThresholdMs = 1500;

    bool sameItem(const PresenceInfo& a, const PresenceInfo& b) {
      return a.kind == b.kind && a.title == b.title && a.seriesName == b.seriesName && a.season == b.season &&
             a.episode == b.episode;
    }
  } // namespace

  bool presenceAllowed(const PresenceInfo& info, const PresencePrivacy& privacy) {
    if (privacy.hidden) return false;
    switch (info.kind) {
    case PresenceInfo::Kind::Movie:
      if (!privacy.movies) return false;
      break;
    case PresenceInfo::Kind::Episode:
      if (!privacy.series) return false;
      break;
    case PresenceInfo::Kind::Music:
      if (!privacy.music) return false;
      break;
    case PresenceInfo::Kind::LiveTv:
      if (!privacy.liveTv) return false;
      break;
    }
    if (privacy.hiddenLibraries.isEmpty()) return true;
    if (!info.librariesKnown) return false;
    for (const QString& id : info.libraryIds)
      if (privacy.hiddenLibraries.contains(id)) return false;
    return true;
  }

  DiscordPresence::DiscordPresence(QStringList endpoints, QObject* parent)
      : QObject(parent), m_ipc(std::move(endpoints)) {
    // Keeps the paused position and the group size current between changes.
    m_refresh.setInterval(15000);
    connect(&m_refresh, &QTimer::timeout, this, &DiscordPresence::publish);
    if (QCoreApplication::instance())
      connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &m_ipc, &DiscordIpc::shutdown);
  }

  void DiscordPresence::configure(bool enabled, const QString& applicationId) {
    m_ipc.configure(enabled, applicationId);
    if (enabled)
      m_refresh.start();
    else
      m_refresh.stop();
    publish();
  }

  void DiscordPresence::setShowPoster(bool show) {
    if (show == m_showPoster) return;
    m_showPoster = show;
    publish();
  }

  void DiscordPresence::setPrivacy(const PresencePrivacy& privacy) {
    if (privacy == m_privacy) return;
    m_privacy = privacy;
    publish();
  }

  void DiscordPresence::update(const std::optional<PresenceInfo>& info) {
    if (!info) {
      m_info.reset();
      m_start = 0;
      m_pausedSince = 0;
      m_positionClock.invalidate();
      publish();
      return;
    }
    if (m_info) {
      const qint64 expected =
          m_info->positionMs + (m_info->playing && m_positionClock.isValid() ? m_positionClock.elapsed() : 0);
      if (!sameItem(*m_info, *info)) m_pausedSince = 0;
      if (!sameItem(*m_info, *info) || std::abs(info->positionMs - expected) > SeekThresholdMs) m_start = 0;
    }
    m_info = info;
    m_positionClock.start();
    publish();
  }

  void DiscordPresence::publish() {
    if (!m_info) {
      m_ipc.setActivity({});
      return;
    }
    PresenceInfo now = *m_info;
    if (now.playing && m_positionClock.isValid()) now.positionMs += m_positionClock.elapsed();
    if (!m_showPoster) {
      now.poster = QUrl();
      now.externalPoster = QUrl();
    }
    const qint64 seconds = m_now();
    m_start = presenceStart(m_start, now.positionMs, now.playing, seconds);
    const bool paused = !now.playing && !now.buffering;
    if (!paused)
      m_pausedSince = 0;
    else if (m_pausedSince == 0)
      m_pausedSince = seconds;
    if (!presenceAllowed(now, m_privacy) || (paused && seconds - m_pausedSince >= PauseHideSeconds)) {
      m_ipc.setActivity({});
      return;
    }
    m_ipc.setActivity(buildActivity(now, m_start, PresenceStyle{m_privacy.showTitle}));
  }

} // namespace jfd::integrations
