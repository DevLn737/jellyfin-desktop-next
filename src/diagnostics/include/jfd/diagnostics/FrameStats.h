#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jfd::diagnostics {

// Summary of presented-frame intervals, in microseconds.
struct FrameSummary
{
  std::size_t frames = 0;
  double meanUs = 0;
  double p50Us = 0;
  double p95Us = 0;
  double p99Us = 0;
  double maxUs = 0;
  // Intervals longer than 1.5 refresh periods (a missed vsync), if a period was given.
  std::size_t longFrames = 0;
};

// Collects intervals between consecutive frame presentations. Not thread-safe:
// call from one thread (the render thread), read after it has stopped or under
// the caller's lock.
class FrameStats
{
public:
  explicit FrameStats(std::size_t reserve = 0);

  // Timestamp of a presented frame, in microseconds on a monotonic clock.
  void addFrame(std::int64_t timestampUs);
  // Forget the previous timestamp, so a pause in presentation (window hidden,
  // a measurement phase boundary) is not counted as one long interval.
  void breakSequence();
  void clear();
  // Drops the recorded intervals but keeps the last timestamp, so the next
  // interval still spans the boundary.
  void clearIntervals() { m_intervals.clear(); }

  const std::vector<std::int64_t>& intervals() const { return m_intervals; }
  FrameSummary summary(double refreshPeriodUs = 0) const;

private:
  std::vector<std::int64_t> m_intervals;
  std::int64_t m_last = -1;
};

// Nearest-rank percentile (0 < p <= 100) of unsorted values; 0 when empty.
double percentile(std::vector<std::int64_t> values, double p);

}
