#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

// The Rich Presence card for what plays ("Listening" for music): title, season
// and episode with the episode's name, or year, a time bar while it plays, a
// poster, up to two links for friends (IMDb, TMDB, Shikimori, MusicBrainz,
// Last.fm) and "watching together (N)" in a SyncPlay group. Pure functions;
// DiscordPresence feeds them.
namespace jfd::integrations {

  // A button on the card. Discord shows buttons to other people only.
  struct PresenceLink {
    QString label;
    QUrl url;

    bool operator==(const PresenceLink&) const = default;
  };

  struct PresenceInfo {
    enum class Kind { Movie, Episode, Music, LiveTv };

    Kind kind = Kind::Movie;
    QString title;      // Movie, episode, track or channel name.
    QString seriesName; // Episodes: shown as the main line.
    QString artist;     // Music: the artist(s); album is the line after it.
    QString album;
    QString program; // Live TV: what is on now.
    QStringList genres;
    std::optional<double> rating; // The community rating (0 to 10).
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<int> episodeEnd; // Double episodes.
    int year = 0;
    qint64 durationMs = 0; // The item's full runtime, not a transcode's length.
    qint64 positionMs = 0;
    bool playing = false;
    bool buffering = false;
    QUrl poster;         // From publicPosterUrl(): on the user's server.
    QUrl externalPoster; // From the server's metadata providers (TMDB, ...).
    QList<PresenceLink> links;
    int groupSize = 0; // SyncPlay participants including us; 0 outside a group.
    // The item's library and the folders above it, for hiding libraries.
    QStringList libraryIds;
    bool librariesKnown = false;
  };

  // How much the card shows. Without the title it says only "Movie",
  // "Series", "Music" or "TV" with the app's icon.
  struct PresenceStyle {
    bool showTitle = true;
  };

  // Discord's image proxy only fetches public images, and the URL must not
  // carry credentials: only https on a public host name, without user info,
  // query (tokens) or fragment. Empty otherwise.
  QUrl publicPosterUrl(const QUrl& serverUrl, const QString& itemId, const QString& imageTag);

  // The links for an item from its provider ids (for an episode, its
  // series'), best first, at most two. Music always gets Last.fm.
  QList<PresenceLink> presenceLinks(PresenceInfo::Kind kind, const QHash<QString, QString>& providerIds,
                                    const QString& artist = {}, const QString& track = {});

  // The timer anchor (seconds since the epoch at which playback started at 0).
  // Kept while paused or buffering; rebased when playing or after a reset (0).
  qint64 presenceStart(qint64 previousStart, qint64 positionMs, bool playing, qint64 nowSeconds);

  // Empty when there is nothing to show (no title).
  QJsonObject buildActivity(const PresenceInfo& info, qint64 start, const PresenceStyle& style = {});

  // The app's icon on the card: the repository's own, which Discord's image
  // proxy can fetch.
  QString presenceLogoUrl();

} // namespace jfd::integrations
