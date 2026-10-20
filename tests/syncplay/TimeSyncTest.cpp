#include <doctest/doctest.h>
#include <jfd/syncplay/TimeSync.h>

using namespace jfd::syncplay;
using namespace std::chrono_literals;

namespace {
  LocalInstant local(std::chrono::milliseconds t) { return LocalInstant::fromSinceOrigin(t); }
  ServerInstant server(std::chrono::milliseconds t) { return ServerInstant::fromSinceOrigin(t); }
} // namespace

TEST_CASE("One exchange gives the NTP offset and round trip") {
  TimeSync sync;
  // Server is 10 s ahead; 40 ms each way; server holds the request 5 ms.
  const auto m = sync.record(local(1000ms), {server(11'040ms), server(11'045ms)}, local(1085ms));
  REQUIRE(m);
  CHECK(m->offset.serverMinusLocal == WallTicks(10'000ms));
  CHECK(m->roundTrip == WallTicks(80ms));
}

TEST_CASE("The lowest round trip of the last eight wins") {
  TimeSync sync;
  // Asymmetric slow exchange: offset estimate is off by 100 ms.
  sync.record(local(0ms), {server(10'300ms), server(10'300ms)}, local(400ms));
  const auto good = sync.record(local(1000ms), {server(11'010ms), server(11'010ms)}, local(1020ms));
  REQUIRE(good);
  CHECK(good->offset.serverMinusLocal == WallTicks(10'000ms));
  CHECK(sync.best()->roundTrip == WallTicks(20ms));
  for (int i = 0; i < 8; ++i)
    sync.record(local(2000ms), {server(12'100ms), server(12'100ms)}, local(2300ms));
  // The good exchange has aged out.
  CHECK(sync.best()->roundTrip == WallTicks(300ms));
}

TEST_CASE("Impossible exchanges are ignored") {
  TimeSync sync;
  CHECK_FALSE(sync.record(local(1000ms), {server(5000ms), server(6000ms)}, local(1100ms)));
  CHECK_FALSE(sync.best());
}

TEST_CASE("Polls every second three times, then every minute") {
  TimeSync sync;
  CHECK(sync.nextPollDelay() == WallTicks(1s));
  for (int i = 0; i < 3; ++i) sync.record(local(0ms), {server(0ms), server(0ms)}, local(10ms));
  CHECK(sync.nextPollDelay() == WallTicks(60s));
}
