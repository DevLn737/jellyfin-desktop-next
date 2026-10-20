#include "Fakes.h"

#include <doctest/doctest.h>
#include <jfd/syncplay/GroupClient.h>
#include <jfd/syncplay/Protocol.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

// Message sequences our client received from the Docker test servers (stock
// 12.2 and the SyncPlay fork), recorded by the drift checks
// (tools/syncplay-agent/run.mjs writes them next to the report). Every
// message must decode, and replayed in order through the group client they
// must keep the membership and turn into the session inputs the core expects.

using namespace jfd::core;
using namespace jfd::syncplay;
using Json = nlohmann::json;

namespace {

  struct SilentTransport final : Transport {
    void request(const HttpRequest&, Reply) override {}
  };

  std::vector<std::filesystem::path> recordings() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(JFD_SYNCPLAY_FIXTURES "/recorded"))
      if (entry.path().extension() == ".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
  }

  Json load(const std::filesystem::path& path) {
    std::ifstream file(path);
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    return Json::parse(text.str());
  }

  template <class T> int count(const std::vector<SessionInput>& inputs) {
    return static_cast<int>(std::count_if(inputs.begin(), inputs.end(),
                                          [](const SessionInput& i) { return std::holds_alternative<T>(i); }));
  }

} // namespace

TEST_CASE("Recorded server sequences decode and replay through the group client") {
  const auto files = recordings();
  REQUIRE(files.size() >= 4);
  int messages = 0;
  for (const auto& path : files) {
    const Json recording = load(path);
    for (const auto& participant : recording["participants"]) {
      CAPTURE(path.filename().string());
      jfd::testing::FakeClock clock;
      SilentTransport transport;
      std::vector<SessionInput> inputs;
      GroupClient client(transport, clock, [&](SessionInput input) { inputs.push_back(std::move(input)); });
      int readies = 0;
      std::int64_t lastMs = 0;

      for (const auto& event : participant["events"]) {
        const std::int64_t ms = event["ms"].get<std::int64_t>();
        CHECK(ms >= lastMs);
        clock.advance(std::chrono::milliseconds(ms - lastMs));
        lastMs = ms;

        if (event.contains("connection")) {
          const std::string state = event["connection"];
          if (state == "ready" && ++readies > 1) client.onConnectionRestored();
          if (state == "disconnected" || state == "resumed") client.onConnectionLost();
          continue;
        }
        if (!event.contains("message")) continue;
        const std::string text = event["message"].dump();
        CAPTURE(text);
        const auto decoded = parseMessage(text);
        REQUIRE(decoded);
        CHECK_FALSE(std::holds_alternative<wire::Other>(*decoded));
        if (const auto* queue = std::get_if<wire::PlayQueue>(&*decoded)) {
          CHECK(queue->reason != QueueChangeReason::Unknown);
          CHECK(queue->lastUpdate);
        }
        if (const auto* command = std::get_if<wire::Command>(&*decoded)) {
          if (command->type != GroupCommandType::Stop) CHECK(command->when);
        }

        const bool wasInGroup = client.isInGroup();
        const auto before = inputs.size();
        client.onMessage(text);
        const std::vector<SessionInput> produced(inputs.begin() + static_cast<std::ptrdiff_t>(before), inputs.end());
        ++messages;

        if (std::holds_alternative<wire::GroupJoined>(*decoded)) {
          CHECK(client.isInGroup());
          CHECK(count<group::Joined>(produced) == 1);
        } else if (std::holds_alternative<wire::GroupLeft>(*decoded)) {
          CHECK_FALSE(client.isInGroup());
        } else if (std::holds_alternative<wire::Command>(*decoded)) {
          CHECK(count<group::Command>(produced) == (wasInGroup ? 1 : 0));
        } else if (std::holds_alternative<wire::PlayQueue>(*decoded)) {
          CHECK(count<group::QueueChanged>(produced) == (wasInGroup ? 1 : 0));
        } else if (std::holds_alternative<wire::StateUpdate>(*decoded)) {
          CHECK(count<group::StateChanged>(produced) == (wasInGroup ? 1 : 0));
        }
      }
    }
  }
  MESSAGE(messages << " recorded messages replayed");
}
