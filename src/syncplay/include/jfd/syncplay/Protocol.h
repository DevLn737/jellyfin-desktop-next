#pragma once

#include <jfd/core/SyncPlay.h>
#include <jfd/core/Time.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The SyncPlay wire format of the 12.2-syncplay.1 server (and stock 12.x):
// WebSocket messages in, HTTP request bodies out. Pure functions, no I/O.
namespace jfd::syncplay {

  using namespace jfd::core;

  // ISO 8601 UTC as the server writes it ("2026-10-11T00:00:01.0000000Z"),
  // also accepting fewer fraction digits and a numeric offset.
  std::optional<ServerInstant> parseServerTime(std::string_view text);
  // Always seven fraction digits and "Z".
  std::string formatServerTime(ServerInstant instant);

  // Messages as decoded, before the group module tags them with an epoch.
  namespace wire {
    struct Command {
      GroupCommandType type = GroupCommandType::Pause;
      std::string groupId;
      PlaylistItemId playlistItem;
      // Missing When means the command applies on receipt.
      std::optional<ServerInstant> when;
      std::optional<ServerInstant> emittedAt;
      MediaTicks position;
      // Missing, null or out-of-range rates mean 1x.
      PlaybackRate baseRate = PlaybackRate::normal();
    };
    // GroupInfoDto: Participants are user names, one per session.
    struct GroupJoined {
      GroupId group;
      std::optional<GroupState> state;
      std::string name;
      std::vector<std::string> participants;
    };
    // UserJoined and UserLeft carry the user name only.
    struct UserJoined {
      std::string name;
    };
    struct UserLeft {
      std::string name;
    };
    struct GroupLeft {};
    struct StateUpdate {
      GroupState state = GroupState::Idle;
      std::string reason;
    };
    struct PlayQueue {
      QueueChangeReason reason = QueueChangeReason::Unknown;
      std::optional<ServerInstant> lastUpdate;
      PlaylistItemId currentPlaylistItem;
      ItemId currentItem;
      MediaTicks startPosition;
      bool isPlaying = false;
      PlaybackRate baseRate = PlaybackRate::normal();
    };
    // NotInGroup, GroupDoesNotExist, JoinGroupDenied, LibraryAccessDenied, ...
    struct Refused {
      std::string type;
    };
    // Any other session message (KeepAlive, ForceKeepAlive, ...).
    struct Other {
      std::string messageType;
    };
  } // namespace wire

  using WireMessage =
      std::variant<wire::Command, wire::GroupJoined, wire::GroupLeft, wire::StateUpdate,
                   wire::PlayQueue, wire::Refused, wire::Other, wire::UserJoined, wire::UserLeft>;

  // Empty when the text is not JSON or a SyncPlay message is malformed.
  std::optional<WireMessage> parseMessage(std::string_view json);

  // A request to the server.
  struct HttpRequest {
    enum class Method { Get, Post };
    Method method = Method::Post;
    std::string path;
    // JSON; empty for requests without a body.
    std::string body;
  };

  HttpRequest requestFor(const SyncPlayMessage& message);
  HttpRequest createGroupRequest(std::string_view name);
  HttpRequest joinGroupRequest(const GroupId& group);
  HttpRequest leaveGroupRequest();
  HttpRequest pingRequest(WallTicks roundTrip);
  HttpRequest utcTimeRequest();

  // GET /GetUtcTime reply.
  struct UtcTimeReply {
    ServerInstant requestReception;
    ServerInstant responseTransmission;
  };
  std::optional<UtcTimeReply> parseUtcTimeReply(std::string_view json);

  // 409 ProblemDetails for SetPlaybackRate: unsupportedParticipants, if present.
  int parseUnsupportedParticipants(std::string_view json);

} // namespace jfd::syncplay
