#pragma once

#include <jfd/integrations/DiscordActivity.h>
#include <jfd/integrations/DiscordIpc.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <optional>

// Discord Rich Presence for what plays, off by default (a setting). Keeps the
// card's timer steady across refreshes, and rebases it on seeks and new items;
// a long pause removes the card.
namespace jfd::integrations {

  // What the user keeps off the card (Settings → Integrations).
  struct PresencePrivacy {
    // "Hide for now": nothing is shown.
    bool hidden = false;
    // Only "Watching a movie", without the title, poster or links.
    bool showTitle = true;
    bool movies = true;
    bool series = true;
    bool music = true;
    bool liveTv = true;
    // Library ids; an item in one of them is not shown, nor one whose
    // library is not known yet.
    QSet<QString> hiddenLibraries;

    bool operator==(const PresencePrivacy&) const = default;
  };

  // Whether the card may show this item at all.
  bool presenceAllowed(const PresenceInfo& info, const PresencePrivacy& privacy);

  class DiscordPresence final : public QObject {
    Q_OBJECT
  public:
    // The public Application ID of the "Jellyfin Desktop" application, the
    // one the 2.x client uses.
    static QString defaultApplicationId() { return QStringLiteral("1557831977250463766"); }

    explicit DiscordPresence(QStringList endpoints = {}, QObject* parent = nullptr);

    void configure(bool enabled, const QString& applicationId = defaultApplicationId());
    // The poster: from the server's metadata providers (TMDB, ...) when
    // they have one, else from the server, whose address everyone who sees
    // the card can then see. Off unless the user opts in
    // (keys::DiscordPoster); off, the card shows the app's icon.
    void setShowPoster(bool show);
    void setPrivacy(const PresencePrivacy& privacy);
    // nullopt: nothing plays; the card is removed.
    void update(const std::optional<PresenceInfo>& info);

    // After this long paused the card is removed; it comes back on play.
    static constexpr qint64 PauseHideSeconds = 10 * 60;

    // Seconds since the epoch; replaced in tests.
    void setClock(std::function<qint64()> nowSeconds) { m_now = std::move(nowSeconds); }

  private:
    void publish();

    DiscordIpc m_ipc;
    QTimer m_refresh;
    std::optional<PresenceInfo> m_info;
    QElapsedTimer m_positionClock;
    qint64 m_start = 0;
    bool m_showPoster = false;
    PresencePrivacy m_privacy;
    qint64 m_pausedSince = 0; // Seconds since the epoch; 0 while it plays.
    std::function<qint64()> m_now = [] { return QDateTime::currentSecsSinceEpoch(); };
  };

} // namespace jfd::integrations
