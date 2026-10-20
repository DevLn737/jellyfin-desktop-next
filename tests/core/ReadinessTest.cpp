#include <doctest/doctest.h>
#include <jfd/core/Readiness.h>

using namespace jfd::core;

namespace {
  MediaTicks ms(std::int64_t value) { return *MediaTicks::fromMilliseconds(value); }
} // namespace

TEST_CASE("Initial start is ready on the first restart with a fresh position") {
  EpochCounter<RequestTag> requests;
  Readiness readiness;
  const auto load = requests.advance();
  readiness.begin(load, ms(10'000), false);
  CHECK_FALSE(readiness.restarted(std::nullopt, false, true));
  CHECK_FALSE(readiness.restarted(ms(10'000), true, true));
  CHECK(readiness.restarted(ms(10'010), false, true) == load);
  CHECK_FALSE(readiness.pending());
}

TEST_CASE("A restart queued before the seek event belongs to the earlier request") {
  EpochCounter<RequestTag> requests;
  Readiness readiness;
  readiness.begin(requests.advance(), ms(0), false);
  const auto seek = requests.advance();
  readiness.begin(seek, ms(60'000), true);
  // Restart of the earlier operation, unpaused so the target does not apply.
  CHECK_FALSE(readiness.restarted(ms(1'000), false, false));
  readiness.seekStarted();
  CHECK(readiness.restarted(ms(60'100), false, false) == seek);
}

TEST_CASE("Paused readiness must be within 500 ms of the target") {
  EpochCounter<RequestTag> requests;
  Readiness readiness;
  const auto seek = requests.advance();
  readiness.begin(seek, ms(30'000), true);
  readiness.seekStarted();
  CHECK_FALSE(readiness.restarted(ms(29'499), false, true));
  CHECK(readiness.pending() == seek);
  CHECK(readiness.restarted(ms(30'500), false, true) == seek);
}

TEST_CASE("A failed request never becomes ready") {
  EpochCounter<RequestTag> requests;
  Readiness readiness;
  const auto first = requests.advance();
  const auto second = requests.advance();
  readiness.begin(second, ms(0), false);
  readiness.failed(first);
  CHECK(readiness.pending() == second);
  readiness.failed(second);
  CHECK_FALSE(readiness.restarted(ms(0), false, true));
}
