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
