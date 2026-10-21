#pragma once

#include <jfd/core/MediaBackend.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/Time.h>

#include <optional>

// Per-file mapping between backend time and Jellyfin's media timeline. One
// instance belongs to one MediaEpoch; a new file gets a new mapping. Ported
// from PlaybackTimeline in the 2.x client and the contract's
// nativePlayback.streamTimeline rules.
namespace jfd::core {

  class Timeline {
  public:
    Timeline() = default;
    Timeline(StreamTimeline stream, MediaTicks requestedStart);

    // Whether the backend should rebase the file's clock to zero.
    // Everything but preserved video starts the backend clock at zero: a
    // Jellyfin HLS transcode with an external audio file starts its
    // timestamps about ten seconds in, which the playlist does not show.
    bool rebasesStartTime() const { return m_stream.kind != StreamTimeline::Kind::PreservedVideo; }

    // Records the source start once the file is open. Preserved timelines need
    // a finite start and fail without one; relative timelines ignore it.
    bool configure(std::optional<double> demuxerStartSeconds);
    bool isReady() const { return m_ready; }

    // Where to start the held file. Empty means the file's natural start: a
    // zero start, or a requested start before the beginning of the stream.
    std::optional<BackendSeconds> startPosition() const;

    // Empty when the position is before the start of the stream (relative
    // transcode with an offset) or the mapping is not ready.
    std::optional<BackendSeconds> toBackend(MediaTicks position) const;
    std::optional<MediaTicks> toMedia(BackendSeconds position) const;
    std::optional<MediaTicks> duration(double backendDurationSeconds) const;

  private:
    StreamTimeline m_stream;
    MediaTicks m_requestedStart;
    double m_sourceStart = 0.0;
    bool m_ready = false;
  };

} // namespace jfd::core
