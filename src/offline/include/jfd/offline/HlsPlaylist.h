#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

// The two playlists of a server's HLS VOD transcode: the master playlist
// names the media playlist, which lists an init segment (EXT-X-MAP) and the
// fMP4 segments with their durations. Jellyfin 12.2 writes the whole media
// playlist up front, from the item's run time, with #EXT-X-ENDLIST.
namespace jfd::offline {

  // The first variant URI of a master playlist, or empty.
  QString hlsFirstVariant(const QByteArray& master);

  struct HlsSegment {
    // As written in the playlist: relative to the playlist's URL.
    QString uri;
    double seconds = 0;
  };

  struct HlsMediaPlaylist {
    // The init segment (EXT-X-MAP URI), or empty when there is none.
    QString mapUri;
    QList<HlsSegment> segments;
    double totalSeconds = 0;

    // A hash of what makes two transcodes of the same item and profile
    // interchangeable segment by segment: whether there is an init segment,
    // the target duration and every segment's duration. URIs are left out:
    // they carry the play session.
    QString signature() const;
  };

  // Nullopt unless the playlist is a complete VOD playlist (#EXTM3U,
  // #EXT-X-ENDLIST, at least one segment, no #EXT-X-PLAYLIST-TYPE other
  // than VOD): an event or live playlist cannot be downloaded.
  std::optional<HlsMediaPlaylist> parseHlsMediaPlaylist(const QByteArray& text);

} // namespace jfd::offline
