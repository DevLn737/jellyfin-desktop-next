#pragma once

#include <jfd/api/Models.h>
#include <jfd/api/Playback.h>

#include <QList>
#include <QString>

#include <optional>

// The narrow hook through which the launcher plays downloaded copies
// (stage 3.10a). src/session does not know where downloads live; the app
// gives the launcher an implementation (offline::OfflineLocalSource).
namespace jfd::session {

  // A complete copy of an item on this computer.
  struct LocalCopy {
    // From the copy's manifest: the server's JSON at download time, read with
    // the same parsers as online.
    api::Item item;
    api::MediaSource source;
    // The media file, a file system path (never a file:// URL).
    QString path;
    // External subtitle files next to it (url: QUrl::fromLocalFile). Embedded
    // tracks, and the fonts of embedded ASS subtitles, are inside the file.
    QList<api::ExternalTrack> externalSubtitles;
    // External audio tracks next to it (the server's "Film.ru.mka" files).
    QList<api::ExternalTrack> externalAudio;
    // Intros and credits, for skipping without the server.
    QList<api::MediaSegment> segments;
    // The original file, byte for byte. A compressed copy (3.10b) numbers
    // its tracks differently.
    bool original = true;
  };

  class LocalSource {
  public:
    virtual ~LocalSource() = default;
    // The copy to play for `itemId` now, if any: always while the server
    // cannot be reached, and while it can when "Play downloads from disk"
    // is on. Must not touch the network.
    virtual std::optional<LocalCopy> find(const QString& itemId) const = 0;
  };

} // namespace jfd::session
