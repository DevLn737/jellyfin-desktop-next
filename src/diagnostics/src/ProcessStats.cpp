#include "jfd/diagnostics/ProcessStats.h"

#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#include <sys/resource.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#endif

namespace jfd::diagnostics {

namespace {
std::int64_t nowUs()
{
  return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32
std::int64_t fileTimeUs(const FILETIME& time)
{
  ULARGE_INTEGER value;
  value.LowPart = time.dwLowDateTime;
  value.HighPart = time.dwHighDateTime;
  return static_cast<std::int64_t>(value.QuadPart / 10);
}
#endif
}

std::optional<ProcessSample> sampleCurrentProcess()
{
  ProcessSample sample;
  sample.timestampUs = nowUs();
#ifdef _WIN32
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return std::nullopt;
  sample.cpuTimeUs = fileTimeUs(kernel) + fileTimeUs(user);
  PROCESS_MEMORY_COUNTERS_EX memory {};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
    return std::nullopt;
  sample.residentBytes = memory.WorkingSetSize;
  sample.privateBytes = memory.PrivateUsage;
#else
  rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0)
    return std::nullopt;
  sample.cpuTimeUs = (static_cast<std::int64_t>(usage.ru_utime.tv_sec) + usage.ru_stime.tv_sec) * 1000000
    + usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
#if defined(__linux__)
  std::ifstream statm("/proc/self/statm");
  std::uint64_t size = 0, resident = 0, shared = 0;
  if (!(statm >> size >> resident >> shared))
    return std::nullopt;
  const auto page = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
  sample.residentBytes = resident * page;
  sample.privateBytes = (resident - shared) * page;
#elif defined(__APPLE__)
  mach_task_basic_info info {};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
    return std::nullopt;
  sample.residentBytes = info.resident_size;
  sample.privateBytes = info.resident_size;
#endif
#endif
  return sample;
}

double cpuPercent(const ProcessSample& from, const ProcessSample& to)
{
  const auto wall = to.timestampUs - from.timestampUs;
  if (wall <= 0)
    return 0;
  return 100.0 * static_cast<double>(to.cpuTimeUs - from.cpuTimeUs) / static_cast<double>(wall);
}

}
