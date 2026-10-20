#include "jfd/diagnostics/FrameStats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace jfd::diagnostics {

FrameStats::FrameStats(std::size_t reserve)
{
  m_intervals.reserve(reserve);
}

void FrameStats::addFrame(std::int64_t timestampUs)
{
  if (m_last >= 0 && timestampUs >= m_last)
    m_intervals.push_back(timestampUs - m_last);
  m_last = timestampUs;
}

void FrameStats::breakSequence()
{
  m_last = -1;
}

void FrameStats::clear()
{
  m_intervals.clear();
  m_last = -1;
}

double percentile(std::vector<std::int64_t> values, double p)
{
  if (values.empty())
    return 0;
  const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(values.size())));
  const std::size_t index = std::clamp<std::size_t>(rank, 1, values.size()) - 1;
  std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
  return static_cast<double>(values[index]);
}

FrameSummary FrameStats::summary(double refreshPeriodUs) const
{
  FrameSummary result;
  result.frames = m_intervals.size();
  if (m_intervals.empty())
    return result;
  const auto total = std::accumulate(m_intervals.begin(), m_intervals.end(), std::int64_t {0});
  result.meanUs = static_cast<double>(total) / static_cast<double>(m_intervals.size());
  result.p50Us = percentile(m_intervals, 50);
  result.p95Us = percentile(m_intervals, 95);
  result.p99Us = percentile(m_intervals, 99);
  result.maxUs = static_cast<double>(*std::max_element(m_intervals.begin(), m_intervals.end()));
  if (refreshPeriodUs > 0)
    result.longFrames = static_cast<std::size_t>(std::count_if(m_intervals.begin(), m_intervals.end(),
      [&](std::int64_t interval) { return static_cast<double>(interval) > 1.5 * refreshPeriodUs; }));
  return result;
}

}
