#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Timers.h>
#include <jfd/syncplay/Protocol.h>
#include <jfd/syncplay/TimeSync.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The client side of a SyncPlay group: turns server messages into
// epoch-tagged session inputs and the session's outgoing messages into HTTP
// requests, keeps time in sync, and rejoins after a lost connection.
namespace jfd::syncplay {

  // Implemented by jellyfin-api with the authenticated session's HTTP client.
  // Replies are delivered later on the session's thread.
  class Transport {
  public:
    virtual ~Transport() = default;
    using Reply = std::function<void(int httpStatus, std::string body)>;
    virtual void request(const HttpRequest& request, Reply reply) = 0;
  };

  class GroupClient final : public SyncPlayOutbox {
  public:
    using InputSink = std::function<void(SessionInput)>;

    GroupClient(Transport& transport, Clock& clock, InputSink sink);

    // What the user does.
    void createGroup(std::string_view name);
    void joinGroup(const GroupId& group);
    void leaveGroup();

    // The session WebSocket (owned by jellyfin-api). Only SyncPlay messages
    // are used; others are ignored.
    void onMessage(std::string_view json);
    // The WebSocket or the server session was lost.
    void onConnectionLost();
    // A fresh session and WebSocket exist and capabilities were registered.
    void onConnectionRestored();

    // GET /GetUtcTime; call again after nextPollDelay().
    void pollTime();
    WallTicks nextPollDelay() const { return m_timeSync.nextPollDelay(); }

    // SyncPlayOutbox
    void send(const SyncPlayMessage& message) override;

    bool isInGroup() const { return m_inGroup; }
    GroupEpoch epoch() const { return m_epoch; }
    const std::optional<GroupId>& desiredGroup() const { return m_desiredGroup; }

  private:
    ServerInstant serverNow() const;
    void handle(const WireMessage& message);
    void endMembership();

    Transport& m_transport;
    Clock& m_clock;
    InputSink m_sink;
    TimeSync m_timeSync;
    ClockOffset m_offset;

    EpochCounter<GroupEpochTag> m_epochs;
    GroupEpoch m_epoch;
    bool m_inGroup = false;
    // The session still holds this membership while the connection is lost.
    bool m_sessionHoldsGroup = false;
    std::optional<GroupId> m_desiredGroup;
  };

} // namespace jfd::syncplay
