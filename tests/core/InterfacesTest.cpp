#include <doctest/doctest.h>
#include <jfd/core/MediaBackend.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Timers.h>

#include <type_traits>
#include <vector>

using namespace jfd::core;

namespace {

  // The interfaces must be implementable without anything outside core.
  struct RecordingOutbox final : SyncPlayOutbox {
    std::vector<SyncPlayMessage> sent;
    void send(const SyncPlayMessage& message) override { sent.push_back(message); }
  };

} // namespace

TEST_CASE("Session inputs and outgoing messages are value types") {
  static_assert(std::is_copy_constructible_v<SessionInput>);
  static_assert(std::is_copy_constructible_v<SyncPlayMessage>);
  static_assert(std::is_abstract_v<MediaBackend>);
  static_assert(std::is_abstract_v<Timers>);

  RecordingOutbox outbox;
  EpochCounter<GroupEpochTag> groups;
  const auto epoch = groups.advance();
  outbox.send(outgoing::RequestSeek{epoch, *MediaTicks::fromMilliseconds(30'000)});
  REQUIRE(outbox.sent.size() == 1);
  CHECK(std::get<outgoing::RequestSeek>(outbox.sent[0]).position.milliseconds() == 30'000);

  SessionInput input = group::Command{};
  CHECK(std::holds_alternative<group::Command>(input));
}

TEST_CASE("Identifiers of different kinds do not mix") {
  static_assert(!std::is_convertible_v<ItemId, PlaylistItemId>);
  static_assert(!std::is_convertible_v<std::string, GroupId>);
  CHECK(PlaylistItemId("a") == PlaylistItemId("a"));
  CHECK(PlaylistItemId().isEmpty());
}
