#pragma once

#include <jfd/core/SessionInput.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/playback/TrackTable.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Runs the track table of the playing file against the playback session:
// selections become intent::SelectTrack, external files intent::AddExternalTrack,
// and the session's snapshot answers the adds. A track the transcoded stream
// does not carry is handed to the app, which opens the stream again.
namespace jfd::playback {

  class TrackController {
  public:
    struct Callbacks {
      // Posts an input to the playback session.
      std::function<void(core::SessionInput)> post;
      // The stream must be requested again with these tracks.
      std::function<void(const command::Reopen&)> reopen;
      // The Jellyfin stream indices now selected, for PlaybackReporter::setTracks.
      std::function<void(std::optional<int> audio, std::optional<int> subtitle)> selectionChanged;
    };

    explicit TrackController(Callbacks callbacks);

    // The file the session is opening as `epoch`, with the tracks it was
    // requested with (the server's defaults when empty).
    void begin(core::MediaEpoch epoch, const api::MediaSource& source,
               const api::PlaybackStream& stream, std::optional<int> audioStreamIndex,
               std::optional<int> subtitleStreamIndex);

    // Selects by Jellyfin stream index; empty disables subtitles.
    void select(TrackKind kind, std::optional<int> streamIndex);

    const TrackTable& table() const { return m_table; }

    void snapshotChanged(const core::SessionSnapshot& snapshot);

  private:
    struct PendingAdd {
      std::uint64_t key = 0;
      // Where the session lists it in SessionSnapshot::externalTracks.
      std::size_t position = 0;
      std::string url;
    };

    void run(std::vector<Command> commands);

    using Selection = std::pair<std::optional<int>, std::optional<int>>;

    Callbacks m_callbacks;
    std::optional<core::MediaEpoch> m_epoch;
    TrackTable m_table;
    std::vector<PendingAdd> m_pending;
    std::size_t m_added = 0;
    std::optional<Selection> m_reported;
  };

} // namespace jfd::playback
