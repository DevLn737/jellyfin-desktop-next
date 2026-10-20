#pragma once

#include <cstdint>
#include <optional>

namespace jfd::diagnostics {

struct ProcessSample
{
  // Monotonic time of the sample.
  std::int64_t timestampUs = 0;
  // User plus kernel CPU time of this process since it started.
  std::int64_t cpuTimeUs = 0;
  // Resident set (Windows: working set).
  std::uint64_t residentBytes = 0;
  // Memory committed to this process alone (Windows: private bytes; Linux:
  // private resident pages from /proc/self/statm, resident minus shared).
  std::uint64_t privateBytes = 0;
};

// Samples the current process. Empty if the platform query fails.
std::optional<ProcessSample> sampleCurrentProcess();

// Average CPU load between two samples, in percent of one logical core.
double cpuPercent(const ProcessSample& from, const ProcessSample& to);

}
