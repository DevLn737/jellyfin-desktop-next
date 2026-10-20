#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/Ids.h>
#include <jfd/core/MediaBackend.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Time.h>
#include <jfd/core/Timers.h>

#include <optional>
#include <string>
#include <variant>

// Everything the playback session reacts to arrives as one SessionInput on one
// queue: user intents, backend events, SyncPlay group messages and timers.
namespace jfd::core {

  // How backend positions of one file relate to Jellyfin's media timeline.
  struct StreamTimeline {
    enum class Kind {
      // Backend time starts at zero; media = backend + offset. The offset is
      // TranscodingOffsetTicks for non-HLS transcodes without copied
      // timestamps and zero for everything else that is not preserved.
      Relative,
      // Original decoder timestamps kept (video in copy-timestamp transcodes):
      // media = backend, duration = source start + stream duration.
      PreservedVideo,
      // Original timestamps, but the backend clock is rebased to zero (music):
      // media = backend + source start.
      PreservedMusic,
    };

    Kind kind = Kind::Relative;
    MediaTicks offset;
  };

  // One file to play, already resolved through PlaybackInfo by the app.
  struct MediaRequest {
    ItemId item;
    MediaSourceId mediaSource;
    // The SyncPlay queue occurrence; empty outside a group.
    PlaylistItemId playlistItem;
    std::string url;
    StreamTimeline timeline;
    MediaTicks start;
    std::optional<MediaTicks> runTime;
    bool startPaused = false;
  };

  namespace intent {
    struct Open {
      MediaRequest media;
    };
    struct Play {};
    struct Pause {};
    struct Seek {
      MediaTicks position;
    };
    // In a group this asks the group; outside it sets the personal rate.
    struct SetRate {
      PlaybackRate rate = PlaybackRate::normal();
    };
    struct Stop {};
  } // namespace intent

  namespace backend {

    // State read from the backend at one instant. Position is empty when the
    // backend could not provide one; the session never invents a zero.
    struct Sample {
      std::optional<BackendSeconds> position;
      bool paused = false;
      bool seeking = false;
      bool buffering = false;
      LocalInstant sampledAt;
    };

    // The file is open and held before playback (see MediaBackend::load).
    struct SourceOpened {
      MediaEpoch epoch;
      // demuxer-start-time; empty or non-finite fails a preserved timeline.
      std::optional<double> demuxerStart;
      std::optional<double> duration;
    };

    // MPV_EVENT_SEEK.
    struct SeekStarted {
      MediaEpoch epoch;
    };

    // MPV_EVENT_PLAYBACK_RESTART, with a fresh sample read after the event
    // queue was drained.
    struct PlaybackRestarted {
      MediaEpoch epoch;
      Sample sample;
    };

    // Periodic position and any pause, seeking or buffering change.
    struct Progress {
      MediaEpoch epoch;
      Sample sample;
    };

    enum class EndReason { EndOfFile, Stopped, Error };

    struct Ended {
      MediaEpoch epoch;
      EndReason reason = EndReason::EndOfFile;
      std::string error;
    };

    struct RequestFailed {
      MediaEpoch epoch;
      RequestId request;
      std::string error;
    };

    struct TrackAdded {
      MediaEpoch epoch;
      RequestId request;
      int trackId = 0;
    };

  } // namespace backend

  struct TimerFired {
    TimerId id;
  };

  using SessionInput =
      std::variant<intent::Open, intent::Play, intent::Pause, intent::Seek, intent::SetRate,
                   intent::Stop, backend::SourceOpened, backend::SeekStarted,
                   backend::PlaybackRestarted, backend::Progress, backend::Ended,
                   backend::RequestFailed, backend::TrackAdded, group::Joined, group::Left,
                   group::ConnectionLost, group::StateChanged, group::Command,
                   group::QueueChanged, group::RateChangeRejected, group::ClockSynced,
                   TimerFired>;

} // namespace jfd::core
