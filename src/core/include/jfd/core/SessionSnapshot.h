#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/Ids.h>
#include <jfd/core/MediaBackend.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Time.h>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// The read-only view of the playback session for the UI, integrations
// (Discord) and diagnostics. Published after every input that changed it.
namespace jfd::core {

  enum class PlaybackPhase {
    Idle,
    // Opening a file or waiting for the backend's first restart.
    Loading,
    Playing,
    Paused,
    // Playing was requested but the backend is waiting for data or a seek.
    Buffering,
    Ended,
    Failed,
  };

  enum class SyncCorrection { None, Speed, Skip };

  struct GroupSnapshot {
    GroupId group;
    std::string name;
    // User names, one per session in the group (see group::Joined).
    std::vector<std::string> participants;
    GroupState state = GroupState::Idle;
    PlaybackRate baseRate = PlaybackRate::normal();
    bool connected = true;
    PlaylistItemId playlistItem;
    // Expected group position minus our position, from the latest sample
    // while the group is playing. Positive means we are behind.
    std::optional<std::int64_t> driftMilliseconds;
    SyncCorrection correction = SyncCorrection::None;
    std::uint32_t correctionsSinceSynced = 0;
    bool operator==(const GroupSnapshot&) const = default;
  };

  // An external audio or subtitle file added to the current file, in the
  // order added. The app finds its own by URL; trackId is the backend's id
  // for intent::SelectTrack once the backend added it.
  struct ExternalTrackState {
    enum class Status { Adding, Added, Failed };
    ExternalTrack track;
    Status status = Status::Adding;
    std::optional<int> trackId;
    std::string error;
    bool operator==(const ExternalTrackState&) const = default;
  };

  struct SessionSnapshot {
    std::uint64_t revision = 0;
    MediaEpoch media;
    PlaybackPhase phase = PlaybackPhase::Idle;
    ItemId item;
    MediaSourceId mediaSource;
    std::string title;
    MediaTicks position;
    std::optional<MediaTicks> duration;
    // What the backend plays at: base (or personal) rate times any correction.
    PlaybackRate effectiveRate = PlaybackRate::normal();
    // The user's own rate, used outside a group and restored after leaving.
    PlaybackRate personalRate = PlaybackRate::normal();
    std::vector<ExternalTrackState> externalTracks;
    std::optional<GroupSnapshot> group;
    std::string error;
    bool operator==(const SessionSnapshot&) const = default;
  };

  namespace notice {
    // The group moved to an item the app has to resolve and open.
    struct GroupItemRequested {
      PlaylistItemId playlistItem;
      ItemId item;
      MediaTicks position;
    };
    // The group refused a rate change; 409 means a participant or the server
    // lacks the playback-rate extension.
    struct GroupRateRejected {
      int httpStatus = 0;
      int unsupportedParticipants = 0;
    };
    // A seek before the start of a stream that cannot seek there (relative
    // transcode with an offset): the app must request a new stream.
    struct StreamReopenNeeded {
      MediaTicks position;
    };
    // The backend did not confirm readiness in time.
    struct ReadyTimedOut {
      MediaTicks target;
    };
    // Next or Previous outside a group: the app's own queue decides what
    // plays and opens it.
    struct QueueStepRequested {
      bool forward = true;
    };
  } // namespace notice

  using SessionNotice =
      std::variant<notice::GroupItemRequested, notice::GroupRateRejected,
                   notice::StreamReopenNeeded, notice::ReadyTimedOut, notice::QueueStepRequested>;

  class SessionObserver {
  public:
    virtual ~SessionObserver() = default;
    virtual void snapshotChanged(const SessionSnapshot& snapshot) = 0;
    virtual void notify(const SessionNotice& notice) = 0;
  };

} // namespace jfd::core
