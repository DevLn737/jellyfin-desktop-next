#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace jfd::diagnostics {

// The application's log file, bounded in size and written without a disk
// flush per line.
//
// open() keeps the previous file as `previous` (one old file only). A line
// that would take the file past maxBytes first moves it to `previous` and
// starts a new one, so the two files together never hold much more than
// twice the limit, however long the app runs at the debug level.
//
// Lines are buffered: an urgent one (a warning or an error) is flushed at
// once with everything before it, so the lines that explain a failure or a
// crash are on disk; the others at most flushInterval later, by the next
// line or a flushIfDue() from a timer.
//
// Not thread-safe: the caller serialises (the log handler's mutex).
class LogFile {
public:
  struct Options {
    std::uintmax_t maxBytes = 10u * 1024 * 1024;
    std::chrono::milliseconds flushInterval {1000};
  };
  using Clock = std::chrono::steady_clock;

  LogFile() = default;
  explicit LogFile(Options options) : m_options(options) {}
  LogFile(const LogFile&) = delete;
  LogFile& operator=(const LogFile&) = delete;

  bool open(const std::filesystem::path& current, const std::filesystem::path& previous);
  bool isOpen() const { return m_out.is_open(); }
  void write(std::string_view line, bool urgent, Clock::time_point now = Clock::now());
  // Flushes buffered lines older than flushInterval.
  void flushIfDue(Clock::time_point now = Clock::now());
  void flush();
  void close();

  // Bytes in the current file.
  std::uintmax_t size() const { return m_size; }
  // Times the file was moved aside while running.
  int rotations() const { return m_rotations; }

private:
  bool start(bool keepPrevious);

  Options m_options;
  std::filesystem::path m_current;
  std::filesystem::path m_previous;
  std::ofstream m_out;
  std::uintmax_t m_size = 0;
  bool m_dirty = false;
  Clock::time_point m_lastFlush {};
  int m_rotations = 0;
};

}
