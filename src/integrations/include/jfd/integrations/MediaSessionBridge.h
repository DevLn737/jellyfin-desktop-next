#pragma once

#include <jfd/core/SessionInput.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/integrations/DiscordActivity.h>
#include <jfd/platform/MediaControls.h>

#include <QObject>
#include <QPointer>
#include <QUrl>

#include <functional>
#include <optional>
#include <vector>

namespace jfd::platform {
  class SleepInhibitor;
}

// Feeds the session's snapshots to the system media controls (SMTC, MPRIS),
// Discord and sleep inhibition, and turns the controls' commands into session
// intents, so in a SyncPlay group they act like the player's own buttons.
namespace jfd::integrations {

  class DiscordPresence;

  // What the snapshot does not carry about the item that plays; the app fills
  // it from the library item it opened.
  struct NowPlayingDetails {
    core::ItemId item; // The details apply only while this item plays.
    PresenceInfo::Kind kind = PresenceInfo::Kind::Movie;
    QString seriesName;
    QString artist; // Music.
    QString album;
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<int> episodeEnd;
    int year = 0;
    QUrl artUrl;       // Media controls; must load without credentials.
    QUrl publicPoster; // Discord: publicPosterUrl() or empty.
    // The app's queue, used outside a group; a group always has both.
    bool hasNext = false;
    bool hasPrevious = false;
  };

  // details may be null or for another item; then only the snapshot is used.
  platform::MediaControlsState mediaControlsState(const core::SessionSnapshot& snapshot,
                                                  const NowPlayingDetails* details);
  // nullopt: nothing to show.
  std::optional<PresenceInfo> presenceInfo(const core::SessionSnapshot& snapshot,
                                           const NowPlayingDetails* details);
  // Playing or waiting for data to play.
  bool keepsDisplayAwake(const core::SessionSnapshot& snapshot);
  std::optional<core::SessionInput> commandInput(platform::MediaCommand command,
                                                 core::PlaybackPhase phase);

  // No Q_OBJECT: it has no signals, and moc of older Qt cannot parse core's
  // C++20 headers.
  class MediaSessionBridge final : public QObject {
  public:
    using Post = std::function<void(core::SessionInput)>;

    // post: session::SessionRuntime::post.
    explicit MediaSessionBridge(Post post, QObject* parent = nullptr);

    // None is owned; each may be added once, before or after snapshots.
    void addControls(platform::MediaControls* controls);
    void setPresence(DiscordPresence* presence);
    void setSleepInhibitor(platform::SleepInhibitor* inhibitor);

    void setDetails(NowPlayingDetails details);
    // A session::SessionHub snapshot handler.
    void update(const core::SessionSnapshot& snapshot);

  private:
    const NowPlayingDetails* currentDetails() const;
    void publish();
    void command(platform::MediaCommand command);
    void seek(qint64 positionMs);

    Post m_post;
    std::vector<QPointer<platform::MediaControls>> m_controls;
    QPointer<DiscordPresence> m_presence;
    QPointer<platform::SleepInhibitor> m_inhibitor;
    core::SessionSnapshot m_snapshot;
    std::optional<NowPlayingDetails> m_details;
    std::optional<platform::MediaControlsState> m_lastControls;
    std::optional<bool> m_lastAwake;
  };

} // namespace jfd::integrations
