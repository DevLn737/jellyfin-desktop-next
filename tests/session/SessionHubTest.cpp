#include <jfd/session/SessionHub.h>

#include <doctest/doctest.h>

#include <optional>
#include <vector>

using namespace jfd;
using jfd::session::SessionHub;

namespace {
  core::SessionSnapshot snapshot(std::uint64_t revision) {
    core::SessionSnapshot s;
    s.revision = revision;
    return s;
  }
} // namespace

TEST_CASE("Every subscriber gets the latest snapshot at once and every change after") {
  SessionHub hub;
  hub.snapshotChanged(snapshot(4));
  std::vector<std::uint64_t> ui, discord;
  auto a = hub.subscribe([&](const core::SessionSnapshot& s) { ui.push_back(s.revision); });
  auto b = hub.subscribe([&](const core::SessionSnapshot& s) { discord.push_back(s.revision); });
  hub.snapshotChanged(snapshot(5));
  CHECK(ui == std::vector<std::uint64_t>{4, 5});
  CHECK(discord == std::vector<std::uint64_t>{4, 5});
  CHECK(hub.latest().revision == 5);
}

TEST_CASE("Notices reach subscribers that listen for them") {
  SessionHub hub;
  int notices = 0;
  auto quiet = hub.subscribe({});
  auto listening = hub.subscribe({}, [&](const core::SessionNotice& n) {
    CHECK(std::holds_alternative<core::notice::QueueStepRequested>(n));
    ++notices;
  });
  hub.notify(core::notice::QueueStepRequested{true});
  CHECK(notices == 1);
}

TEST_CASE("A subscription ends when it is destroyed or reset, also from inside a call") {
  SessionHub hub;
  int calls = 0;
  {
    auto scoped = hub.subscribe([&](const core::SessionSnapshot&) { ++calls; });
    CHECK(calls == 1);
  }
  hub.snapshotChanged(snapshot(1));
  CHECK(calls == 1);

  // The first handler ends the second one's subscription mid-round.
  std::optional<SessionHub::Subscription> second;
  int secondCalls = 0;
  auto first = hub.subscribe([&](const core::SessionSnapshot& s) {
    if (s.revision == 2 && second) second->reset();
  });
  second = hub.subscribe([&](const core::SessionSnapshot&) { ++secondCalls; });
  CHECK(secondCalls == 1);
  hub.snapshotChanged(snapshot(2));
  CHECK(secondCalls == 1);

  // Moving keeps the subscription; the moved-from one ends nothing.
  int moved = 0;
  auto original = hub.subscribe([&](const core::SessionSnapshot&) { ++moved; });
  SessionHub::Subscription target = std::move(original);
  original.reset();
  hub.snapshotChanged(snapshot(3));
  CHECK(moved == 2);
}

TEST_CASE("A subscriber added during a round is not called again in that round") {
  SessionHub hub;
  std::vector<SessionHub::Subscription> added;
  int lateCalls = 0;
  auto adder = hub.subscribe([&](const core::SessionSnapshot& s) {
    if (s.revision == 1)
      added.push_back(hub.subscribe([&](const core::SessionSnapshot&) { ++lateCalls; }));
  });
  hub.snapshotChanged(snapshot(1));
  CHECK(lateCalls == 1);
  hub.snapshotChanged(snapshot(2));
  CHECK(lateCalls == 2);
}
