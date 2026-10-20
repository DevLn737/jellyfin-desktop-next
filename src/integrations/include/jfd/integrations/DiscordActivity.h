#pragma once

#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <optional>

// The Rich Presence card for what plays: title, season and episode or year,
// a time bar, the poster when the server is public, and "watching together
// (N)" in a SyncPlay group. Pure functions; DiscordPresence feeds them.
namespace jfd::integrations {

  struct PresenceInfo {
    enum class Kind { Movie, Episode };

    Kind kind = Kind::Movie;
    QString title;      // Movie or episode name.
    QString seriesName; // Episodes: shown as the main line.
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<int> episodeEnd; // Double episodes.
    int year = 0;
    qint64 durationMs = 0; // The item's full runtime, not a transcode's length.
    qint64 positionMs = 0;
    bool playing = false;
    bool buffering = false;
    QUrl poster;        // From publicPosterUrl(); empty shows the app icon.
    int groupSize = 0;  // SyncPlay participants including us; 0 outside a group.
  };

  // Discord's image proxy only fetches public images, and the URL must not
  // carry credentials: only https on a public host name, without user info,
  // query (tokens) or fragment. Empty otherwise.
  QUrl publicPosterUrl(const QUrl& serverUrl, const QString& itemId, const QString& imageTag);

  // The timer anchor (seconds since the epoch at which playback started at 0).
  // Kept while paused or buffering; rebased when playing or after a reset (0).
  qint64 presenceStart(qint64 previousStart, qint64 positionMs, bool playing, qint64 nowSeconds);

  // Empty when there is nothing to show (no title).
  QJsonObject buildActivity(const PresenceInfo& info, qint64 start);

} // namespace jfd::integrations
