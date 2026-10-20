#include <jfd/diagnostics/FrameStats.h>

#include <doctest/doctest.h>

using jfd::diagnostics::FrameStats;
using jfd::diagnostics::percentile;

TEST_CASE("percentile uses the nearest rank")
{
  CHECK(percentile({}, 95) == 0);
  CHECK(percentile({5}, 95) == 5);
  CHECK(percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 50) == 5);
  CHECK(percentile({10, 9, 8, 7, 6, 5, 4, 3, 2, 1}, 95) == 10);
  CHECK(percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 90) == 9);
}

TEST_CASE("frame intervals and long frames")
{
  FrameStats stats;
  std::int64_t t = 1000;
  stats.addFrame(t);
  for (int i = 0; i < 99; ++i)
    stats.addFrame(t += 16667);
  stats.addFrame(t += 33334); // one missed vsync
  const auto summary = stats.summary(16667);
  CHECK(summary.frames == 100);
  CHECK(summary.p50Us == 16667);
  CHECK(summary.p95Us == 16667);
  CHECK(summary.maxUs == 33334);
  CHECK(summary.longFrames == 1);
}

TEST_CASE("a broken sequence does not count the gap")
{
  FrameStats stats;
  stats.addFrame(0);
  stats.addFrame(10);
  stats.breakSequence();
  stats.addFrame(1000000);
  stats.addFrame(1000010);
  CHECK(stats.intervals().size() == 2);
  CHECK(stats.summary().maxUs == 10);
}

TEST_CASE("clearing intervals keeps the boundary interval")
{
  FrameStats stats;
  stats.addFrame(0);
  stats.addFrame(10);
  stats.clearIntervals();
  stats.addFrame(25);
  REQUIRE(stats.intervals().size() == 1);
  CHECK(stats.intervals().front() == 15);
}
