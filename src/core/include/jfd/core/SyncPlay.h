#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/Ids.h>
#include <jfd/core/Time.h>

#include <string>
#include <variant>
#include <vector>

// SyncPlay as the playback session sees it: typed group messages coming in
// (decoded and epoch-tagged by the syncplay module) and the messages the
// session sends to the group. Field meanings follow the 12.2-syncplay.1
// contract; see docs/core-interfaces.md.
namespace jfd::core {

  enum class GroupState { Idle, Waiting, Paused, Playing };

  enum class GroupCommandType { Unpause, Pause, Stop, Seek };

  // Why the play queue changed (PlayQueue update Reason).
  enum class QueueChangeReason {
    NewPlaylist,
    SetCurrentItem,
    NextItem,
    PreviousItem,
    RemoveItems,
    MoveItem,
    Queue,
    QueueNext,
    RepeatMode,
    ShuffleMode,
    Unknown,
  };

  namespace group {

    // Membership began: on join, on create, or again after a reconnect. The
    // syncplay module issues a new epoch each time.
    // Participants are user names as the server lists them: one entry per
    // session, so a user in the group from two devices appears twice.
    // SyncPlay 12.2 has no per-participant ready or buffering state; the
    // group state (Waiting) is the only sign that someone is not ready.
    struct Joined {
      GroupEpoch epoch;
      GroupId group;
      std::string name;
      std::vector<std::string> participants;
    };

    // UserJoined or UserLeft: the full list after the change.
    struct ParticipantsChanged {
      GroupEpoch epoch;
      std::vector<std::string> participants;
    };

    struct Left {
      GroupEpoch epoch;
    };

    // The WebSocket or session was lost. The syncplay module rejoins on its own
    // and reports a new Joined when it has.
    struct ConnectionLost {
      GroupEpoch epoch;
    };

    struct StateChanged {
      GroupEpoch epoch;
      GroupState state = GroupState::Idle;
    };

    // SyncPlayCommand. Position and When describe the same moment; base rate
    // is the group rate (missing or invalid on the wire means 1x).
    struct Command {
      GroupEpoch epoch;
      GroupCommandType type = GroupCommandType::Pause;
      PlaylistItemId playlistItem;
      ServerInstant when;
      ServerInstant emittedAt;
      MediaTicks position;
      PlaybackRate baseRate = PlaybackRate::normal();
    };

    struct QueueChanged {
      GroupEpoch epoch;
      QueueChangeReason reason = QueueChangeReason::Unknown;
      ServerInstant lastUpdate;
      // Empty when nothing is playing.
      PlaylistItemId currentPlaylistItem;
      ItemId currentItem;
      MediaTicks startPosition;
      bool isPlaying = false;
      PlaybackRate baseRate = PlaybackRate::normal();
    };

    // The server refused POST SyncPlay/SetPlaybackRate. 409 means a member
    // lacks the playback-rate extension (or the server has none).
    struct RateChangeRejected {
      GroupEpoch epoch;
      int httpStatus = 0;
      int unsupportedParticipants = 0;
    };

    // A new TimeSync estimate. Not tied to a group: the offset is the server's.
    struct ClockSynced {
      ClockOffset offset;
      WallTicks roundTrip{0};
    };

  } // namespace group

  namespace outgoing {

    // POST SyncPlay/Ready and SyncPlay/Buffering. When is server time,
    // position is the real logical position, isPlaying the actual unpaused state.
    struct Ready {
      GroupEpoch epoch;
      ServerInstant when;
      MediaTicks position;
      bool isPlaying = false;
      PlaylistItemId playlistItem;
      bool operator==(const Ready&) const = default;
    };

    struct Buffering {
      GroupEpoch epoch;
      ServerInstant when;
      MediaTicks position;
      bool isPlaying = false;
      PlaylistItemId playlistItem;
      bool operator==(const Buffering&) const = default;
    };

    struct RequestPause {
      GroupEpoch epoch;
      bool operator==(const RequestPause&) const = default;
    };

    struct RequestUnpause {
      GroupEpoch epoch;
      bool operator==(const RequestUnpause&) const = default;
    };

    struct RequestSeek {
      GroupEpoch epoch;
      MediaTicks position;
      bool operator==(const RequestSeek&) const = default;
    };

    struct RequestPlaybackRate {
      GroupEpoch epoch;
      PlaybackRate rate = PlaybackRate::normal();
      bool operator==(const RequestPlaybackRate&) const = default;
    };

    struct RequestStop {
      GroupEpoch epoch;
      bool operator==(const RequestStop&) const = default;
    };

    // POST SyncPlay/NextItem and SyncPlay/PreviousItem. The server ignores
    // the request unless the item is still the current one, so a repeated
    // press skips only once.
    struct RequestNextItem {
      GroupEpoch epoch;
      PlaylistItemId playlistItem;
      bool operator==(const RequestNextItem&) const = default;
    };

    struct RequestPreviousItem {
      GroupEpoch epoch;
      PlaylistItemId playlistItem;
      bool operator==(const RequestPreviousItem&) const = default;
    };

  } // namespace outgoing

  using SyncPlayMessage =
      std::variant<outgoing::Ready, outgoing::Buffering, outgoing::RequestPause,
                   outgoing::RequestUnpause, outgoing::RequestSeek,
                   outgoing::RequestPlaybackRate, outgoing::RequestStop,
                   outgoing::RequestNextItem, outgoing::RequestPreviousItem>;

  // Implemented by the syncplay module. Messages whose epoch is no longer the
  // current group epoch are dropped there, not sent.
  class SyncPlayOutbox {
  public:
    virtual ~SyncPlayOutbox() = default;
    virtual void send(const SyncPlayMessage& message) = 0;
  };

} // namespace jfd::core
