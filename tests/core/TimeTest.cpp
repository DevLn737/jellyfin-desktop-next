#include <doctest/doctest.h>
#include <jfd/core/Time.h>

#include <limits>

using namespace jfd::core;

TEST_CASE("MediaTicks converts milliseconds and ticks exactly") {
  const auto position = MediaTicks::fromMilliseconds(30'000);
  REQUIRE(position);
  CHECK(position->ticks() == 300'000'000);
  CHECK(position->milliseconds() == 30'000);
  CHECK(position->seconds() == doctest::Approx(30.0));
}

TEST_CASE("MediaTicks rejects negative and overflowing values") {
  CHECK_FALSE(MediaTicks::fromTicks(-1));
  CHECK_FALSE(MediaTicks::fromMilliseconds(-1));
  CHECK_FALSE(MediaTicks::fromMilliseconds(std::numeric_limits<std::int64_t>::max()));
  CHECK_FALSE(MediaTicks::fromSeconds(-0.5));
  CHECK_FALSE(MediaTicks::fromSeconds(std::numeric_limits<double>::infinity()));
  CHECK_FALSE(MediaTicks::fromSeconds(std::numeric_limits<double>::quiet_NaN()));
  CHECK_FALSE(MediaTicks::fromSeconds(1e300));
}

TEST_CASE("MediaTicks rounds backend seconds to the nearest millisecond") {
  CHECK(MediaTicks::fromSeconds(12.3456)->milliseconds() == 12'346);
  CHECK(MediaTicks::fromSeconds(0.0004)->ticks() == 0);
}

TEST_CASE("differenceMilliseconds is signed") {
  const auto a = *MediaTicks::fromMilliseconds(1'000);
  const auto b = *MediaTicks::fromMilliseconds(1'400);
  CHECK(differenceMilliseconds(a, b) == -400);
  CHECK(differenceMilliseconds(b, a) == 400);
}

TEST_CASE("PlaybackRate base range is 0.25x to 5x") {
  CHECK(PlaybackRate::base(1.25)->perMille() == 1250);
  CHECK(PlaybackRate::base(0.25)->perMille() == 250);
  CHECK(PlaybackRate::base(5.0)->perMille() == 5000);
  CHECK_FALSE(PlaybackRate::base(0.2));
  CHECK_FALSE(PlaybackRate::base(5.01));
  CHECK_FALSE(PlaybackRate::base(std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("PlaybackRate corrections may go below the minimum base rate") {
  CHECK(PlaybackRate::effective(0.2)->perMille() == 200);
  CHECK_FALSE(PlaybackRate::effective(0.19));
  CHECK(PlaybackRate::normal().value() == doctest::Approx(1.0));
}

TEST_CASE("MediaDelta is signed and rounds to milliseconds once") {
  const auto a = *MediaTicks::fromMilliseconds(1'000);
  const auto b = *MediaTicks::fromMilliseconds(1'400);
  CHECK((a - b).ticks() == -4'000'000);
  CHECK((a - b).milliseconds() == -400);
  CHECK(MediaDelta::fromTicks(5'000).milliseconds() == 1);
  CHECK(MediaDelta::fromTicks(-5'000).milliseconds() == -1);
  CHECK(MediaDelta::fromTicks(4'999).milliseconds() == 0);
  CHECK(MediaDelta::fromTicks(-7).abs().ticks() == 7);
}

TEST_CASE("advance fails before the start of the timeline") {
  const auto a = *MediaTicks::fromMilliseconds(100);
  CHECK(advance(a, MediaDelta::fromMilliseconds(50))->milliseconds() == 150);
  CHECK(advance(a, MediaDelta::fromMilliseconds(-100))->ticks() == 0);
  CHECK_FALSE(advance(a, MediaDelta::fromMilliseconds(-101)));
}

TEST_CASE("Rates scale wall time to media time and back") {
  const auto rate = *PlaybackRate::base(1.5);
  const WallTicks twoSeconds = std::chrono::seconds(2);
  CHECK(mediaElapsed(twoSeconds, rate).milliseconds() == 3'000);
  CHECK(mediaElapsed(-twoSeconds, rate).milliseconds() == -3'000);
  CHECK(wallElapsed(MediaDelta::fromMilliseconds(3'000), rate) == twoSeconds);
  CHECK(wallElapsed(MediaDelta::fromMilliseconds(-3'000), rate) == -twoSeconds);
  // round(1 tick * 1.25) = 1, round(3 ticks * 1.25) = 4: rounding, not truncation.
  CHECK(mediaElapsed(WallTicks(3), *PlaybackRate::base(1.25)).ticks() == 4);
}

TEST_CASE("ClockOffset converts between local and server instants") {
  const ClockOffset offset{WallTicks(std::chrono::seconds(10))};
  const auto local = LocalInstant::fromSinceOrigin(std::chrono::seconds(5));
  const auto server = offset.toServer(local);
  CHECK(server.sinceOrigin() == std::chrono::seconds(15));
  CHECK(offset.toLocal(server) == local);
}

TEST_CASE("PlaybackRate per-mille constructors keep the two ranges apart") {
  CHECK(PlaybackRate::basePerMille(250));
  CHECK_FALSE(PlaybackRate::basePerMille(249));
  CHECK(PlaybackRate::effectivePerMille(200));
  CHECK_FALSE(PlaybackRate::effectivePerMille(5001));
}
