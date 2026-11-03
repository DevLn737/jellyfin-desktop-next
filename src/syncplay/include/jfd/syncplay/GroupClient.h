#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Timers.h>
#include <jfd/syncplay/Protocol.h>
#include <jfd/syncplay/TimeSync.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    // Runs task after delay on the session's thread (a retry). Without one,
    // retries run at once.
    using Schedule = std::function<void(WallTicks delay, std::function<void()> task)>;

    // How often a rejoin after a reconnect is tried before the membership
    // ends locally, and the first wait between tries (doubling after each).
    static constexpr int RejoinAttempts = 4;
    static constexpr WallTicks RejoinBackoff = std::chrono::seconds(1);
    // A failed Ready or Buffering report is sent again once after this.
    static constexpr WallTicks ReadyRetryDelay = std::chrono::milliseconds(500);

    GroupClient(Transport& transport, Clock& clock, InputSink sink, Schedule schedule = {});

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
    void rejoin(int attempt);
    void later(WallTicks delay, std::function<void()> task);
    void sendReport(const SyncPlayMessage& message, bool retried);

    Transport& m_transport;
    Clock& m_clock;
    InputSink m_sink;
    Schedule m_schedule;
    // Expires with the client, so a late reply or retry does nothing.
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    // Advances with every rejoin round and every membership change, so a
    // late reply or retry of an earlier round is ignored.
    std::uint64_t m_rejoinSerial = 0;
    // Advances with every Ready or Buffering sent: only the newest is retried.
    std::uint64_t m_reportSerial = 0;
    bool m_connectionLost = false;
    TimeSync m_timeSync;
    ClockOffset m_offset;

    EpochCounter<GroupEpochTag> m_epochs;
    GroupEpoch m_epoch;
    bool m_inGroup = false;
    // The session still holds this membership while the connection is lost.
    bool m_sessionHoldsGroup = false;
    std::optional<GroupId> m_desiredGroup;
    std::vector<std::string> m_participants;
  };

} // namespace jfd::syncplay
