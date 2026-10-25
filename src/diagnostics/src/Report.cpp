#include <jfd/diagnostics/Redact.h>
#include <jfd/diagnostics/Report.h>

#include <algorithm>
#include <sstream>

namespace jfd::diagnostics {

namespace {

// The last maxBytes of a log, starting at a line start, with a note when
// something was cut.
std::string tail(const std::string& text, std::size_t maxBytes)
{
  if (text.size() <= maxBytes)
    return text;
  std::size_t from = text.size() - maxBytes;
  const std::size_t lineStart = text.find('\n', from);
  from = lineStart == std::string::npos ? from : lineStart + 1;
  return "[earlier lines cut: " + std::to_string(from) + " bytes]\n" + text.substr(from);
}

// Line by line: the matching works on a line, and a long log stays cheap.
std::string redactLines(const std::string& text)
{
  std::string out;
  out.reserve(text.size());
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    const bool last = end == std::string::npos;
    if (last)
      end = text.size();
    out += redactSecrets(std::string_view(text).substr(start, end - start));
    if (!last)
      out.push_back('\n');
    start = end + 1;
  }
  return out;
}

std::string orUnknown(const std::string& text)
{
  return text.empty() ? "unknown" : text;
}

std::string joinLines(const std::vector<std::string>& lines)
{
  std::string out;
  for (const std::string& line : lines) {
    out += line;
    out.push_back('\n');
  }
  return out;
}

std::string summary(const ReportInput& in)
{
  std::ostringstream out;
  out << "Jellyfin Desktop diagnostic report\n"
      << "Created: " << orUnknown(in.createdAt) << "\n\n"
      << "Client: " << orUnknown(in.clientVersion) << "\n"
      << "Qt: " << orUnknown(in.qtVersion) << "\n"
      << "System: " << orUnknown(in.osName) << " (" << orUnknown(in.cpuArchitecture) << ")\n"
      << "Language: " << orUnknown(in.language) << "\n"
      << "Graphics: " << orUnknown(in.gpu) << "\n"
      << "mpv: " << orUnknown(in.mpvVersion) << "\n"
      << "FFmpeg: " << orUnknown(in.ffmpegVersion) << "\n"
      << "Hardware decoding: setting " << orUnknown(in.hwdecSetting) << ", in use " << orUnknown(in.hwdecCurrent)
      << "\n"
      << "Server: Jellyfin " << orUnknown(in.serverVersion) << "\n\n"
      << "Settings\n";
  for (const auto& [key, value] : in.settings)
    out << "  " << key << " = " << value << "\n";
  return out.str();
}

}

std::vector<ZipEntry> buildReport(const ReportInput& input)
{
  std::vector<ZipEntry> entries;
  entries.push_back({"report.txt", redactLines(summary(input))});
  entries.push_back({"log.txt", redactLines(tail(input.log, MaxLogBytes))});
  entries.push_back({"log-previous.txt", redactLines(tail(input.previousLog, MaxLogBytes))});
  entries.push_back({"session-journal.txt", redactLines(joinLines(input.sessionJournal))});
  entries.push_back({"stats.txt", redactLines(joinLines(input.statsSnapshots))});

  // Whatever the inputs, the archive stays under the limit: trim the biggest
  // entry's start until it fits.
  const auto total = [&] {
    std::size_t sum = 0;
    for (const ZipEntry& entry : entries)
      sum += entry.data.size() + entry.name.size() * 2 + 200;
    return sum;
  };
  while (total() > MaxReportBytes) {
    ZipEntry& biggest = *std::max_element(entries.begin(), entries.end(), [](const ZipEntry& a, const ZipEntry& b) {
      return a.data.size() < b.data.size();
    });
    const std::size_t excess = total() - MaxReportBytes;
    biggest.data = tail(biggest.data, biggest.data.size() - std::min(biggest.data.size() - 1, excess + 1024));
  }
  return entries;
}

}
