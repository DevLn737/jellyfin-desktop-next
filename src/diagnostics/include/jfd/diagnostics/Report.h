#pragma once

#include <jfd/diagnostics/Zip.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace jfd::diagnostics {

// What goes into the one-click diagnostic report. The app fills it in; the
// builder only formats, trims and hides secrets, so it needs no Qt.
struct ReportInput
{
  // "2026-10-25 19:30:00 UTC".
  std::string createdAt;
  std::string clientVersion;
  std::string qtVersion;
  std::string osName;
  std::string cpuArchitecture;
  std::string language;
  // The graphics API and the GPU Qt renders on.
  std::string gpu;
  std::string mpvVersion;
  std::string ffmpegVersion;
  // The "Hardware decoding" setting and what mpv actually uses (hwdec-current).
  std::string hwdecSetting;
  std::string hwdecCurrent;
  std::string serverVersion;
  // "section/key", value as text.
  std::vector<std::pair<std::string, std::string>> settings;
  // Whole log files; only their ends are kept.
  std::string log;
  std::string previousLog;
  // SessionJournal::lines().
  std::vector<std::string> sessionJournal;
  // One line each: the process (memory, CPU) and the presented frames over
  // the last intervals, oldest first.
  std::vector<std::string> statsSnapshots;
};

// The most of one log file kept in the report.
constexpr std::size_t MaxLogBytes = 2 * 1024 * 1024;
// The report as a whole stays under this, whatever the inputs.
constexpr std::size_t MaxReportBytes = 10 * 1024 * 1024;

// report.txt, log.txt, log-previous.txt, session-journal.txt and stats.txt,
// with server access keys, passwords and server addresses replaced by ***
// (redactSecrets) in everything.
std::vector<ZipEntry> buildReport(const ReportInput& input);

}
