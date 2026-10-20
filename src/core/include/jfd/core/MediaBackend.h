#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/Time.h>

#include <compare>
#include <optional>
#include <string>

// What the playback session asks of a media player. The mpv implementation
// lives in the media-backend module; tests use a fake. Every command carries
// the MediaEpoch of the file it is meant for, and the backend tags every event
// it reports with the epoch of the file that produced it (see SessionInput.h).
namespace jfd::core {

  // A position as the backend reports it (mpv time-pos, seconds), before the
  // per-file timeline mapping turns it into MediaTicks.
  struct BackendSeconds {
    double value = 0.0;
    constexpr auto operator<=>(const BackendSeconds&) const = default;
  };

  struct BackendLoad {
    std::string url;
    // Rebase the file's clock to start at zero (mpv rebase-start-time=yes).
    // Used for music in streams that keep original timestamps.
    bool rebaseStartTime = false;
    bool startPaused = false;
  };

  enum class TrackKind { Audio, Subtitle };

  struct ExternalTrack {
    TrackKind kind = TrackKind::Audio;
    std::string url;
    std::string title;
    std::string language;
    bool operator==(const ExternalTrack&) const = default;
  };

  class MediaBackend {
  public:
    virtual ~MediaBackend() = default;

    // Opens a file and holds it before playback starts until start() is
    // called (mpv on_preloaded hook). The backend reports SourceOpened once
    // the demuxer start time is known.
    virtual void load(MediaEpoch epoch, const BackendLoad& request) = 0;

    // Releases a file held by load(). Without a position the file starts at
    // its natural beginning.
    virtual void start(MediaEpoch epoch, std::optional<BackendSeconds> position) = 0;

    // Absolute, exact seek. Failure is reported as RequestFailed.
    virtual void seek(MediaEpoch epoch, RequestId request, BackendSeconds target) = 0;

    virtual void setPaused(MediaEpoch epoch, bool paused) = 0;

    // The effective rate, base rate times any temporary SyncPlay correction.
    virtual void setRate(MediaEpoch epoch, PlaybackRate rate) = 0;

    // Track ids are the backend's own (mpv aid/sid); none disables the type.
    virtual void selectTrack(MediaEpoch epoch, TrackKind kind, std::optional<int> trackId) = 0;

    // Asynchronous; reported as TrackAdded or RequestFailed.
    virtual void addExternalTrack(MediaEpoch epoch, RequestId request,
                                  const ExternalTrack& track) = 0;

    // Stops and unloads the file. Events of this epoch may still arrive and
    // are dropped by the session.
    virtual void stop(MediaEpoch epoch) = 0;
  };

} // namespace jfd::core
