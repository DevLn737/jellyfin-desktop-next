#include "SessionHarness.h"

#include <doctest/doctest.h>
#include <jfd/syncplay/GroupClient.h>

#include <deque>
#include <memory>
#include <nlohmann/json.hpp>

using namespace jfd::core;
using namespace jfd::syncplay;
using namespace jfd::testing;
using namespace std::chrono_literals;

namespace {

  struct FakeTransport final : Transport {
    struct Pending {
      HttpRequest request;
      Reply reply;
    };
    std::deque<Pending> pending;
    void request(const HttpRequest& r, Reply reply) override {
      pending.push_back({r, std::move(reply)});
    }
    std::vector<std::string> paths() const {
      std::vector<std::string> result;
      for (const auto& p : pending) result.push_back(p.request.path);
      return result;
    }
    bool sent(const std::string& path) const {
      for (const auto& p : pending)
        if (p.request.path == path) return true;
      return false;
    }
  };

  struct Fixture {
    FakeClock clock;
    FakeTransport transport;
    std::vector<SessionInput> inputs;
    // Retries the client scheduled, run by runScheduled().
    std::vector<std::pair<WallTicks, std::function<void()>>> scheduled;
    GroupClient client{transport, clock, [this](SessionInput input) { inputs.push_back(input); },
                       [this](WallTicks delay, std::function<void()> task) {
                         scheduled.emplace_back(delay, std::move(task));
                       }};

    void runScheduled() {
      auto tasks = std::move(scheduled);
      scheduled.clear();
      for (auto& [delay, task] : tasks) task();
    }

    // Answers the oldest pending request.
    void reply(int status, std::string body = {}) {
      REQUIRE_FALSE(transport.pending.empty());
      auto pending = std::move(transport.pending.front());
      transport.pending.pop_front();
      pending.reply(status, std::move(body));
    }

    template <class T> std::vector<T> all() const {
      std::vector<T> result;
      for (const auto& i : inputs)
        if (const auto* value = std::get_if<T>(&i)) result.push_back(*value);
      return result;
    }

    void joined(const std::string& group = "g1") {
      client.onMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":")" + group +
                       R"(","Type":"GroupJoined","Data":{"GroupId":")" + group +
                       R"(","State":"Idle"}}})");
    }
  };

  const char* pauseCommand = R"({"MessageType":"SyncPlayCommand","Data":{"GroupId":"g1",
    "PlaylistItemId":"p1","When":"2026-10-20T07:00:00.0000000Z","PositionTicks":10000,
    "Command":"Pause","EmittedAt":"2026-10-20T07:00:00.0000000Z","PlaybackRate":1.25}})";

} // namespace

TEST_CASE("Joining issues a new epoch and tags group messages with it") {
  Fixture f;
  f.client.onMessage(pauseCommand);
  CHECK(f.inputs.empty());

  f.joined();
  REQUIRE(f.all<group::Joined>().size() == 1);
  const GroupEpoch epoch = f.all<group::Joined>()[0].epoch;
  CHECK(epoch.isValid());
  CHECK(f.all<group::Joined>()[0].group == GroupId("g1"));

  f.client.onMessage(pauseCommand);
  const auto commands = f.all<group::Command>();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].epoch == epoch);
  CHECK(commands[0].baseRate.perMille() == 1250);
  CHECK(commands[0].playlistItem == PlaylistItemId("p1"));
}

TEST_CASE("Participants come with the join and follow UserJoined and UserLeft") {
  Fixture f;
  f.client.onMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":"g1",
    "Type":"GroupJoined","Data":{"GroupId":"g1","GroupName":"Movie night","State":"Idle",
    "Participants":["anna"]}}})");
  const auto joined = f.all<group::Joined>();
  REQUIRE(joined.size() == 1);
  CHECK(joined[0].name == "Movie night");
  CHECK(joined[0].participants == std::vector<std::string>{"anna"});

  const auto user = [&](const char* type, const char* name) {
    f.client.onMessage(std::string(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":"g1","Type":")") +
                       type + R"(","Data":")" + name + R"("}})");
  };
  user("UserJoined", "boris");
  user("UserJoined", "anna");
  user("UserLeft", "anna");
  user("UserLeft", "nobody");
  const auto changes = f.all<group::ParticipantsChanged>();
  REQUIRE(changes.size() == 3);
  CHECK(changes[0].participants == std::vector<std::string>{"anna", "boris"});
  CHECK(changes[2].participants == std::vector<std::string>{"boris", "anna"});
  CHECK(changes[2].epoch == joined[0].epoch);
}

TEST_CASE("Outgoing messages of an earlier membership are not sent") {
  Fixture f;
  f.joined();
  const GroupEpoch first = f.client.epoch();
  f.client.onConnectionLost();
  CHECK(f.all<group::ConnectionLost>().size() == 1);
  f.client.send(outgoing::RequestPause{first});
  CHECK(f.transport.pending.empty());

  f.client.onConnectionRestored();
  REQUIRE(f.transport.sent("/SyncPlay/Join"));
  f.joined();
  CHECK(f.client.epoch() != first);
  f.client.send(outgoing::RequestPause{first});
  f.client.send(outgoing::RequestPause{f.client.epoch()});
  CHECK(f.transport.paths().back() == "/SyncPlay/Pause");
  CHECK(f.transport.pending.size() == 2);
}

TEST_CASE("A failed rejoin ends the membership the session still holds") {
  Fixture f;
  f.joined();
  const GroupEpoch epoch = f.client.epoch();
  f.client.onConnectionLost();
  f.client.onConnectionRestored();
  f.client.onMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"Type":"GroupDoesNotExist","Data":""}})");
  REQUIRE(f.all<group::Left>().size() == 1);
  CHECK(f.all<group::Left>()[0].epoch == epoch);
  CHECK_FALSE(f.client.desiredGroup());
}

TEST_CASE("A refused rate change becomes RateChangeRejected for that membership") {
  Fixture f;
  f.joined();
  f.client.send(outgoing::RequestPlaybackRate{f.client.epoch(), *PlaybackRate::base(1.5)});
  REQUIRE(f.transport.pending.size() == 1);
  f.transport.pending[0].reply(409, R"({"status":409,"unsupportedParticipants":1})");
  const auto rejected = f.all<group::RateChangeRejected>();
  REQUIRE(rejected.size() == 1);
  CHECK(rejected[0].httpStatus == 409);
  CHECK(rejected[0].unsupportedParticipants == 1);
}

TEST_CASE("Time polls produce clock offsets and report the round trip to the group") {
  Fixture f;
  f.joined();
  f.client.pollTime();
  REQUIRE(f.transport.pending.size() == 1);
  CHECK(f.transport.pending[0].request.path == "/GetUtcTime");
  const LocalInstant sent = f.clock.now();
  f.clock.advance(40ms);
  const ServerInstant serverAt = ServerInstant::fromSinceOrigin(sent.sinceOrigin() + 5s + 20ms);
  f.transport.pending[0].reply(
      200, R"({"RequestReceptionTime":")" + formatServerTime(serverAt) +
               R"(","ResponseTransmissionTime":")" + formatServerTime(serverAt) + R"("})");
  const auto synced = f.all<group::ClockSynced>();
  REQUIRE(synced.size() == 1);
  CHECK(synced[0].offset.serverMinusLocal == WallTicks(5s));
  CHECK(synced[0].roundTrip == WallTicks(40ms));
  CHECK(f.transport.sent("/SyncPlay/Ping"));
}

TEST_CASE("An idle Stop without When applies now") {
  Fixture f;
  f.joined();
  f.client.onMessage(R"({"MessageType":"SyncPlayCommand","Data":{"Command":"Stop","PlaybackRate":2}})");
  const auto commands = f.all<group::Command>();
  REQUIRE(commands.size() == 1);
  CHECK(commands[0].type == GroupCommandType::Stop);
  CHECK(commands[0].when == ClockOffset{}.toServer(f.clock.now()));
  CHECK(commands[0].baseRate.perMille() == 2000);
}

TEST_CASE("Group client and playback session together answer a group seek") {
  FakeClock clock;
  FakeTimers timers;
  RecordingBackend backend;
  RecordingObserver observer;
  FakeTransport transport;
  std::unique_ptr<PlaybackSession> session;
  GroupClient client{transport, clock, [&session](SessionInput input) { session->post(input); }};
  session = std::make_unique<PlaybackSession>(SessionPorts{backend, clock, timers, client, observer});
  // The first time poll was answered (the fakes share one clock).
  session->post(group::ClockSynced{ClockOffset{}, WallTicks(0)});

  client.onMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":"g1","Type":"GroupJoined","Data":{"State":"Waiting"}}})");
  client.onMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"Type":"PlayQueue","Data":{
    "Reason":"NewPlaylist","LastUpdate":"2026-10-20T07:00:00Z",
    "Playlist":[{"ItemId":"item","PlaylistItemId":"p1"}],"PlayingItemIndex":0,
    "StartPositionTicks":0,"IsPlaying":false,"PlaybackRate":1}}})");
  const auto requested = observer.all<notice::GroupItemRequested>();
  REQUIRE(requested.size() == 1);
  CHECK(requested[0].item == ItemId("item"));

  MediaRequest media;
  media.item = ItemId("item");
  media.playlistItem = PlaylistItemId("p1");
  media.url = "http://server/video";
  session->post(intent::Open{media});
  const MediaEpoch epoch = backend.last<command::Load>()->epoch;
  session->post(backend::SourceOpened{epoch, 0.0, 100.0});
  session->post(backend::PlaybackRestarted{
      epoch, {BackendSeconds{0.0}, true, false, false, clock.now()}});
  REQUIRE(transport.sent("/SyncPlay/Ready"));
  transport.pending.clear();

  client.onMessage(R"({"MessageType":"SyncPlayCommand","Data":{"GroupId":"g1","PlaylistItemId":"p1",
    "When":"1970-01-01T00:00:00Z","PositionTicks":300000000,"Command":"Seek","PlaybackRate":1}})");
  REQUIRE(backend.last<command::Seek>());
  session->post(backend::SeekStarted{epoch});
  session->post(backend::PlaybackRestarted{
      epoch, {BackendSeconds{30.0}, true, false, false, clock.now()}});
  REQUIRE(transport.pending.size() == 1);
  CHECK(transport.pending[0].request.path == "/SyncPlay/Ready");
  CHECK(nlohmann::json::parse(transport.pending[0].request.body)["PositionTicks"] == 300'000'000);
}

TEST_CASE("A reconnect forgets the clock offset") {
  Fixture f;
  f.joined();
  f.client.onConnectionLost();
  f.client.onConnectionRestored();
  CHECK(f.all<group::ClockLost>().size() == 1);
}

TEST_CASE("A failed rejoin is retried with backoff, then the membership ends with a reason") {
  Fixture f;
  f.joined();
  const GroupEpoch epoch = f.client.epoch();
  f.client.onConnectionLost();
  f.client.onConnectionRestored();
  for (int attempt = 0; attempt < GroupClient::RejoinAttempts; ++attempt) {
    REQUIRE(f.transport.pending.size() == 1);
    CHECK(f.transport.pending.front().request.path == "/SyncPlay/Join");
    CHECK(f.all<group::Left>().empty());
    f.reply(attempt % 2 == 0 ? 503 : 0);
    if (attempt + 1 < GroupClient::RejoinAttempts) {
      REQUIRE(f.scheduled.size() == 1);
      CHECK(f.scheduled.front().first == GroupClient::RejoinBackoff * (1 << attempt));
      f.runScheduled();
    }
  }
  CHECK(f.transport.pending.empty());
  CHECK(f.scheduled.empty());
  const auto failed = f.all<group::RequestFailed>();
  REQUIRE(failed.size() == 1);
  CHECK(failed[0].request == group::Request::Rejoin);
  CHECK(failed[0].epoch == epoch);
  REQUIRE(f.all<group::Left>().size() == 1);
  CHECK(f.all<group::Left>()[0].epoch == epoch);
  CHECK_FALSE(f.client.desiredGroup());
}

TEST_CASE("A rejoin that succeeds on retry keeps the group") {
  Fixture f;
  f.joined();
  f.client.onConnectionLost();
  f.client.onConnectionRestored();
  f.reply(500);
  f.runScheduled();
  f.reply(204);
  f.joined();
  CHECK(f.all<group::Left>().empty());
  CHECK(f.client.isInGroup());
  CHECK(f.scheduled.empty());
}

TEST_CASE("No rejoin retry runs while the connection is lost again") {
  Fixture f;
  f.joined();
  f.client.onConnectionLost();
  f.client.onConnectionRestored();
  f.reply(500);
  f.client.onConnectionLost();
  f.runScheduled();
  CHECK(f.transport.pending.empty());
  CHECK(f.all<group::Left>().empty());
}

TEST_CASE("A leave in flight when the connection drops ends the membership on reconnect") {
  Fixture f;
  f.joined();
  f.client.leaveGroup();
  REQUIRE(f.transport.sent("/SyncPlay/Leave"));
  f.transport.pending.clear();
  f.client.onConnectionLost();
  CHECK(f.all<group::Left>().empty());
  f.client.onConnectionRestored();
  CHECK_FALSE(f.transport.sent("/SyncPlay/Join"));
  CHECK(f.all<group::Left>().size() == 1);
}

TEST_CASE("A failed leave leaves locally") {
  Fixture f;
  f.joined();
  f.client.leaveGroup();
  f.reply(500);
  CHECK(f.all<group::Left>().size() == 1);
  CHECK_FALSE(f.client.isInGroup());
  REQUIRE(f.all<group::RequestFailed>().size() == 1);
  CHECK(f.all<group::RequestFailed>()[0].request == group::Request::Leave);
}

TEST_CASE("A failed Ready is sent again once; a second failure is reported") {
  Fixture f;
  f.joined();
  const outgoing::Ready ready{f.client.epoch(), ServerInstant{}, MediaTicks{}, false, PlaylistItemId("p1")};
  f.client.send(ready);
  f.reply(500);
  CHECK(f.all<group::RequestFailed>().empty());
  REQUIRE(f.scheduled.size() == 1);
  CHECK(f.scheduled.front().first == GroupClient::ReadyRetryDelay);
  f.runScheduled();
  REQUIRE(f.transport.pending.size() == 1);
  CHECK(f.transport.pending.front().request.path == "/SyncPlay/Ready");
  f.reply(0);
  const auto failed = f.all<group::RequestFailed>();
  REQUIRE(failed.size() == 1);
  CHECK(failed[0].request == group::Request::Ready);
  CHECK(failed[0].httpStatus == 0);
  CHECK(f.scheduled.empty());
}

TEST_CASE("A failed Ready replaced by a newer report is not retried") {
  Fixture f;
  f.joined();
  const outgoing::Ready ready{f.client.epoch(), ServerInstant{}, MediaTicks{}, false, PlaylistItemId("p1")};
  f.client.send(ready);
  f.client.send(outgoing::Buffering{f.client.epoch(), ServerInstant{}, MediaTicks{}, true, PlaylistItemId("p1")});
  f.reply(500);
  CHECK(f.scheduled.empty());
  f.reply(204);
  CHECK(f.all<group::RequestFailed>().empty());
}

TEST_CASE("Failed create and join requests reach the session journal") {
  Fixture f;
  f.client.createGroup("Movie night");
  f.reply(403);
  f.client.joinGroup(GroupId("g1"));
  f.reply(0);
  const auto failed = f.all<group::RequestFailed>();
  REQUIRE(failed.size() == 2);
  CHECK(failed[0].request == group::Request::Create);
  CHECK(failed[0].httpStatus == 403);
  CHECK(failed[1].request == group::Request::Join);
}
