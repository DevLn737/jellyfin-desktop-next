#include <doctest/doctest.h>
#include <jfd/syncplay/Protocol.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

using namespace jfd::syncplay;
using Json = nlohmann::json;

namespace {
  Json loadFixtures() {
    std::ifstream file(JFD_SYNCPLAY_FIXTURES "/syncplay-source-derived.json");
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    return Json::parse(text.str());
  }

  std::int64_t unixMilliseconds(ServerInstant instant) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(instant.sinceOrigin()).count();
  }
} // namespace

TEST_CASE("Server times parse with seven fraction digits and round-trip") {
  const auto t = parseServerTime("2026-10-11T00:00:01.0000000Z");
  REQUIRE(t);
  CHECK(unixMilliseconds(*t) == 1791676801000);
  CHECK(formatServerTime(*t) == "2026-10-11T00:00:01.0000000Z");

  const auto precise = parseServerTime("2026-10-20T07:04:57.1234567Z");
  REQUIRE(precise);
  CHECK(precise->sinceOrigin().count() % 10'000'000 == 1'234'567);
  CHECK(formatServerTime(*precise) == "2026-10-20T07:04:57.1234567Z");

  CHECK(parseServerTime("2026-10-20T07:04:57.12Z")->sinceOrigin().count() % 10'000'000 ==
        1'200'000);
  CHECK(*parseServerTime("2026-10-20T10:04:57+03:00") == *parseServerTime("2026-10-20T07:04:57Z"));
  CHECK_FALSE(parseServerTime("2026-13-20T07:04:57Z"));
  CHECK_FALSE(parseServerTime("yesterday"));
  CHECK_FALSE(parseServerTime("2026-10-20T07:04:57.Z"));
}

TEST_CASE("Contract fixtures decode to the expected commands and rates") {
  const Json fixtures = loadFixtures();
  int checked = 0;
  for (const auto& fixture : fixtures["messages"]) {
    CAPTURE(fixture["name"].get<std::string>());
    const auto message = parseMessage(fixture["payload"].dump());
    REQUIRE(message);
    const Json& payload = fixture["payload"];
    if (payload["MessageType"] == "SyncPlayCommand") {
      const auto* command = std::get_if<wire::Command>(&*message);
      REQUIRE(command);
      const Json& data = payload["Data"];
      if (data.contains("PositionTicks"))
        CHECK(command->position.ticks() == data["PositionTicks"].get<std::int64_t>());
      if (data.contains("PlaybackRate") && data["PlaybackRate"].is_number())
        CHECK(command->baseRate.value() == doctest::Approx(data["PlaybackRate"].get<double>()));
      else
        CHECK(command->baseRate == PlaybackRate::normal());
      if (data.contains("When"))
        CHECK(unixMilliseconds(*command->when) ==
              fixture["expected"]["metrics"]["whenUtcMs"].get<std::int64_t>());
      ++checked;
    }
  }
  CHECK(checked == 6);
}

TEST_CASE("Group updates decode") {
  auto joined = parseMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":"abc",
    "Type":"GroupJoined","Data":{"GroupId":"abc","GroupName":"x","State":"Paused"}}})");
  REQUIRE(joined);
  REQUIRE(std::holds_alternative<wire::GroupJoined>(*joined));
  CHECK(std::get<wire::GroupJoined>(*joined).group == GroupId("abc"));
  CHECK(std::get<wire::GroupJoined>(*joined).state == GroupState::Paused);
  CHECK(std::get<wire::GroupJoined>(*joined).name == "x");
  CHECK(std::get<wire::GroupJoined>(*joined).participants.empty());

  auto withParticipants = parseMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":"abc",
    "Type":"GroupJoined","Data":{"GroupId":"abc","GroupName":"x","State":"Idle",
    "Participants":["anna","boris"],"LastUpdatedAt":"2026-10-20T07:00:00.0000000Z"}}})");
  CHECK(std::get<wire::GroupJoined>(*withParticipants).participants ==
        std::vector<std::string>{"anna", "boris"});

  auto userJoined = parseMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":"abc",
    "Type":"UserJoined","Data":"boris"}})");
  CHECK(std::get<wire::UserJoined>(*userJoined).name == "boris");
  auto userLeft = parseMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"GroupId":"abc",
    "Type":"UserLeft","Data":"boris"}})");
  CHECK(std::get<wire::UserLeft>(*userLeft).name == "boris");

  auto state = parseMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"Type":"StateUpdate",
    "Data":{"State":"Waiting","Reason":"Buffer"}}})");
  REQUIRE(state);
  CHECK(std::get<wire::StateUpdate>(*state).state == GroupState::Waiting);

  auto queue = parseMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"Type":"PlayQueue",
    "Data":{"Reason":"NewPlaylist","LastUpdate":"2026-10-20T07:00:00.0000000Z",
    "Playlist":[{"ItemId":"i0","PlaylistItemId":"p0"},{"ItemId":"i1","PlaylistItemId":"p1"}],
    "PlayingItemIndex":1,"StartPositionTicks":300000000,"IsPlaying":true,"PlaybackRate":1.5}}})");
  REQUIRE(queue);
  const auto& q = std::get<wire::PlayQueue>(*queue);
  CHECK(q.reason == QueueChangeReason::NewPlaylist);
  CHECK(q.currentPlaylistItem == PlaylistItemId("p1"));
  CHECK(q.currentItem == ItemId("i1"));
  CHECK(q.startPosition.milliseconds() == 30'000);
  CHECK(q.isPlaying);
  CHECK(q.baseRate.perMille() == 1500);

  auto refused = parseMessage(R"({"MessageType":"SyncPlayGroupUpdate","Data":{"Type":"GroupDoesNotExist","Data":""}})");
  REQUIRE(refused);
  CHECK(std::holds_alternative<wire::Refused>(*refused));

  CHECK(std::holds_alternative<wire::Other>(*parseMessage(R"({"MessageType":"KeepAlive"})")));
}

TEST_CASE("Malformed SyncPlay messages are rejected, not guessed") {
  CHECK_FALSE(parseMessage("not json"));
  CHECK_FALSE(parseMessage(R"({"MessageType":"SyncPlayCommand","Data":{"Command":"Dance"}})"));
  // A seek must say where to.
  CHECK_FALSE(parseMessage(R"({"MessageType":"SyncPlayCommand","Data":{"Command":"Seek",
    "When":"2026-10-20T07:00:00Z"}})"));
  CHECK_FALSE(parseMessage(R"({"MessageType":"SyncPlayCommand","Data":{"Command":"Pause",
    "PositionTicks":-5,"When":"2026-10-20T07:00:00Z"}})"));
  // Out-of-range rates fall back to 1x as the contract requires.
  const auto fast = parseMessage(R"({"MessageType":"SyncPlayCommand","Data":{"Command":"Pause",
    "PositionTicks":5,"When":"2026-10-20T07:00:00Z","PlaybackRate":9}})");
  CHECK(std::get<wire::Command>(*fast).baseRate == PlaybackRate::normal());
}

TEST_CASE("Outgoing messages become the documented requests") {
  outgoing::Ready ready;
  ready.when = *parseServerTime("2026-10-20T07:00:00.5000000Z");
  ready.position = *MediaTicks::fromMilliseconds(12'000);
  ready.isPlaying = true;
  ready.playlistItem = PlaylistItemId("p1");
  auto request = requestFor(ready);
  CHECK(request.path == "/SyncPlay/Ready");
  const Json body = Json::parse(request.body);
  CHECK(body["When"] == "2026-10-20T07:00:00.5000000Z");
  CHECK(body["PositionTicks"] == 120'000'000);
  CHECK(body["IsPlaying"] == true);
  CHECK(body["PlaylistItemId"] == "p1");

  request = requestFor(outgoing::RequestPlaybackRate{{}, *PlaybackRate::base(1.25)});
  CHECK(request.path == "/SyncPlay/SetPlaybackRate");
  CHECK(Json::parse(request.body)["PlaybackRate"].get<double>() == doctest::Approx(1.25));

  CHECK(requestFor(outgoing::RequestSeek{{}, *MediaTicks::fromMilliseconds(1)}).body ==
        R"({"PositionTicks":10000})");
  CHECK(requestFor(outgoing::RequestPause{}).path == "/SyncPlay/Pause");
  CHECK(requestFor(outgoing::RequestUnpause{}).path == "/SyncPlay/Unpause");
  request = requestFor(outgoing::RequestNextItem{{}, PlaylistItemId("p1")});
  CHECK(request.path == "/SyncPlay/NextItem");
  CHECK(request.body == R"({"PlaylistItemId":"p1"})");
  CHECK(requestFor(outgoing::RequestPreviousItem{{}, PlaylistItemId("p1")}).path ==
        "/SyncPlay/PreviousItem");
  CHECK(joinGroupRequest(GroupId("g")).body == R"({"GroupId":"g"})");
  CHECK(utcTimeRequest().method == HttpRequest::Method::Get);
}

TEST_CASE("409 ProblemDetails reports unsupported participants") {
  CHECK(parseUnsupportedParticipants(
            R"({"type":"x","status":409,"code":"SyncPlayPlaybackRateNotSupported","unsupportedParticipants":2})") == 2);
  CHECK(parseUnsupportedParticipants("{}") == 0);
  CHECK(parseUnsupportedParticipants("") == 0);
}
