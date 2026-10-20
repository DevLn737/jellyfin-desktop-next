#include <jfd/diagnostics/ProcessStats.h>

#include <doctest/doctest.h>

#include <vector>

using namespace jfd::diagnostics;

TEST_CASE("the current process can be sampled")
{
  const auto first = sampleCurrentProcess();
  REQUIRE(first.has_value());
  CHECK(first->residentBytes > 0);
  CHECK(first->privateBytes > 0);

  // Burn some CPU and touch memory so both counters move.
  std::vector<char> memory(32 * 1024 * 1024, 1);
  volatile unsigned sum = 0;
  for (unsigned i = 0; i < 50'000'000; ++i)
    sum = sum + i;
  const auto second = sampleCurrentProcess();
  REQUIRE(second.has_value());
  CHECK(second->cpuTimeUs > first->cpuTimeUs);
  CHECK(second->timestampUs > first->timestampUs);
  CHECK(cpuPercent(*first, *second) > 0);
  CHECK(memory[memory.size() - 1] == 1);
}
