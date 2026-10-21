#include <doctest/doctest.h>
#include <jfd/core/Timeline.h>

#include <limits>

using namespace jfd::core;

namespace {
  MediaTicks ms(std::int64_t value) { return *MediaTicks::fromMilliseconds(value); }
} // namespace

TEST_CASE("Relative timeline adds the transcoding offset exactly once") {
  Timeline timeline({StreamTimeline::Kind::Relative, ms(600'000)}, ms(605'000));
  CHECK(timeline.isReady());
  // The backend clock starts at zero whatever the stream's first timestamp.
  CHECK(timeline.rebasesStartTime());
  CHECK(timeline.configure(std::nullopt));
  CHECK(timeline.startPosition()->value == doctest::Approx(5.0));
  CHECK(timeline.toMedia({12.5})->milliseconds() == 612'500);
  CHECK(timeline.toBackend(ms(610'000))->value == doctest::Approx(10.0));
  CHECK(timeline.duration(100.0)->milliseconds() == 700'000);
}

TEST_CASE("Relative timeline cannot seek before the start of the stream") {
  Timeline timeline({StreamTimeline::Kind::Relative, ms(600'000)}, ms(600'000));
  CHECK_FALSE(timeline.toBackend(ms(599'999)));
  CHECK(timeline.toBackend(ms(600'000))->value == doctest::Approx(0.0));
}

TEST_CASE("Preserved video keeps original timestamps") {
  Timeline timeline({StreamTimeline::Kind::PreservedVideo, {}}, ms(30'000));
  CHECK_FALSE(timeline.isReady());
  CHECK_FALSE(timeline.startPosition());
  CHECK(timeline.configure(4.0));
  CHECK_FALSE(timeline.rebasesStartTime());
  CHECK(timeline.startPosition()->value == doctest::Approx(30.0));
  CHECK(timeline.toMedia({31.0})->milliseconds() == 31'000);
  CHECK(timeline.duration(100.0)->milliseconds() == 104'000);
}

TEST_CASE("Preserved music maps the rebased clock back to original time") {
  Timeline timeline({StreamTimeline::Kind::PreservedMusic, {}}, ms(30'000));
  CHECK(timeline.rebasesStartTime());
  CHECK(timeline.configure(10.0));
  CHECK(timeline.startPosition()->value == doctest::Approx(20.0));
  CHECK(timeline.toMedia({5.0})->milliseconds() == 15'000);
  CHECK(timeline.toBackend(ms(5'000))->value == doctest::Approx(0.0));
  CHECK(timeline.duration(50.0)->milliseconds() == 60'000);
}

TEST_CASE("A preserved timeline without a finite source start fails visibly") {
  Timeline timeline({StreamTimeline::Kind::PreservedMusic, {}}, ms(0));
  CHECK_FALSE(timeline.configure(std::nullopt));
  CHECK_FALSE(timeline.configure(std::numeric_limits<double>::quiet_NaN()));
  CHECK_FALSE(timeline.isReady());
  CHECK_FALSE(timeline.toMedia({1.0}));
}

TEST_CASE("A zero start begins at the first packet") {
  Timeline timeline({StreamTimeline::Kind::PreservedVideo, {}}, ms(0));
  CHECK(timeline.configure(3600.0));
  CHECK_FALSE(timeline.startPosition());
}

TEST_CASE("Backend positions are never invented") {
  Timeline timeline({StreamTimeline::Kind::Relative, {}}, ms(0));
  CHECK_FALSE(timeline.toMedia({-1.0}));
  CHECK_FALSE(timeline.toMedia({std::numeric_limits<double>::infinity()}));
}
