#include <jfd/diagnostics/ProcessStats.h>

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <vector>

using namespace jfd::diagnostics;

TEST_CASE("the current process can be sampled")
{
  const auto first = sampleCurrentProcess();
  REQUIRE(first.has_value());
  CHECK(first->residentBytes > 0);
  CHECK(first->privateBytes > 0);

  // Touch memory, then burn CPU until the counter moves: Windows counts CPU
  // time in scheduler ticks (about 15.6 ms), so a fixed amount of work can
  // finish within one tick on a fast machine.
  std::vector<char> memory(32 * 1024 * 1024, 1);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::optional<ProcessSample> second;
  volatile unsigned sum = 0;
  do
  {
    for (unsigned i = 0; i < 5'000'000; ++i)
      sum = sum + i;
    second = sampleCurrentProcess();
  } while (second && second->cpuTimeUs == first->cpuTimeUs && std::chrono::steady_clock::now() < deadline);
  REQUIRE(second.has_value());
  CHECK(second->cpuTimeUs > first->cpuTimeUs);
  CHECK(second->timestampUs > first->timestampUs);
  CHECK(cpuPercent(*first, *second) > 0);
  CHECK(memory[memory.size() - 1] == 1);
}
