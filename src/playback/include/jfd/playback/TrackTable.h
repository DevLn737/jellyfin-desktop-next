#pragma once

#include <jfd/api/Models.h>
#include <jfd/api/Playback.h>
#include <jfd/core/MediaBackend.h>

#include <QList>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <vector>

// The audio and subtitle tracks of one playing stream, named by Jellyfin's
// stream index, and what it takes to select one in mpv.
//
// Embedded tracks map to mpv track ids by their order among embedded streams
// of the same type (api::embeddedTrackId). External subtitles and audio are
// added to mpv on first selection; mpv adds asynchronously and selects the
// track it added, so a reply for a selection that was changed in the meantime
// re-asserts the current choice instead of switching to the late track.
// Selecting a track the transcoded stream does not carry needs the stream to
// be reopened with new stream indices.
namespace jfd::playback {

  using core::TrackKind;

  struct TrackInfo {
    int streamIndex = -1;
    TrackKind kind = TrackKind::Audio;
    QString title;
    QString language;
    QString codec;
    bool isDefault = false;
    bool isForced = false;
    bool isExternal = false;
  };

  namespace command {
    // Select an mpv track, or disable the type.
    struct Select {
      TrackKind kind = TrackKind::Audio;
      std::optional<int> trackId;
    };
    // Add an external file to mpv; reply with trackAdded/trackFailed(key).
    struct Add {
      std::uint64_t key = 0;
      core::ExternalTrack track;
    };
    // The stream must be requested again with these indices (-1: no
    // subtitles), keeping the position.
    struct Reopen {
      std::optional<int> audioStreamIndex;
      int subtitleStreamIndex = -1;
    };
  } // namespace command

  using Command = std::variant<command::Select, command::Add, command::Reopen>;

  class TrackTable {
  public:
    TrackTable() = default;
    TrackTable(const api::MediaSource& source, const api::PlaybackStream& stream);

    QList<TrackInfo> tracks(TrackKind kind) const;

    // The stream index chosen for a type; empty for none (subtitles off).
    std::optional<int> selected(TrackKind kind) const;

    // The server's defaults (DefaultAudioStreamIndex and
    // DefaultSubtitleStreamIndex), or the indices the stream was opened with.
    std::vector<Command> selectInitial(std::optional<int> audio, std::optional<int> subtitle);

    // Selects a track by stream index; empty disables the type. An unknown
    // index changes nothing.
    std::vector<Command> select(TrackKind kind, std::optional<int> streamIndex);

    std::vector<Command> trackAdded(std::uint64_t key, int trackId);
    std::vector<Command> trackFailed(std::uint64_t key);

    // Tracks that failed to load are left out of menus.
    bool failed(int streamIndex) const;

  private:
    struct Entry {
      TrackInfo info;
      // mpv track id once known: embedded from the start, external once added.
      std::optional<int> trackId;
      std::optional<QUrl> url;
      QString deliveryMethod;
      // Present in the stream mpv plays (always, unless transcoded).
      bool inStream = true;
      // A transcode's subtitle drawn into the picture by the server.
      bool burnedIn = false;
      bool failed = false;
      std::uint64_t pendingKey = 0;
    };

    Entry* find(TrackKind kind, int streamIndex);
    const Entry* find(TrackKind kind, int streamIndex) const;
    std::vector<Command> apply(TrackKind kind);

    bool m_transcoded = false;
    std::vector<Entry> m_entries;
    std::optional<int> m_audio;
    std::optional<int> m_subtitle;
    std::map<std::uint64_t, std::pair<TrackKind, int>> m_pending;
    std::uint64_t m_nextKey = 1;
  };

} // namespace jfd::playback
