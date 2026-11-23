#include <jfd/diagnostics/Crash.h>
#include <jfd/diagnostics/Report.h>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#ifndef _WIN32
#  include <csignal>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

using namespace jfd::diagnostics;
namespace fs = std::filesystem;

namespace {

// A fresh folder under the system's temporary folder, removed at the end.
struct TempFolder
{
  fs::path path;
  TempFolder()
  {
    std::random_device random;
    path = fs::temp_directory_path() / ("jfd-crash-test-" + std::to_string(random()) + "-" +
                                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(path);
  }
  ~TempFolder()
  {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

void touch(const fs::path& file, const std::string& text = "x")
{
  std::ofstream(file, std::ios::binary) << text;
}

#ifndef _WIN32
std::string readAll(const fs::path& file)
{
  std::ifstream in(file, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
#endif

} // namespace

TEST_CASE("the running marker tells a crashed session from a clean exit")
{
  TempFolder dir;
  // First start: nothing before it.
  CHECK_FALSE(beginSession(dir.path, "version 1, started 10:00"));
  CHECK(fs::exists(dir.path / RunningMarkerName));
  // Clean exit, next start: nothing to report.
  endSession(dir.path);
  CHECK_FALSE(fs::exists(dir.path / RunningMarkerName));
  CHECK_FALSE(beginSession(dir.path, "version 1, started 11:00"));
  // That session never reached endSession (crash, killed, power loss): the
  // next start gets its marker.
  const auto previous = beginSession(dir.path, "version 2, started 12:00");
  REQUIRE(previous);
  CHECK(*previous == "version 1, started 11:00");
  // The marker is now this session's.
  const auto again = beginSession(dir.path, "version 2, started 13:00");
  REQUIRE(again);
  CHECK(*again == "version 2, started 12:00");
  // An empty marker still counts.
  touch(dir.path / RunningMarkerName, "");
  const auto empty = beginSession(dir.path, "x");
  REQUIRE(empty);
  CHECK(*empty == "unknown");
}

TEST_CASE("the marker folder is made when it is not there")
{
  TempFolder dir;
  const fs::path logs = dir.path / "logs" / "deeper";
  CHECK_FALSE(beginSession(logs, "x"));
  CHECK(fs::exists(logs / RunningMarkerName));
}

TEST_CASE("only the newest five crashes keep their files")
{
  TempFolder dir;
  // Eight crashes, some with a dump and a log, plus files that are not
  // crash files.
  for (int i = 1; i <= 8; ++i) {
    const std::string stem = "crash-2026110" + std::to_string(i) + "-120000-42";
    touch(dir.path / (stem + ".txt"));
    if (i % 2 == 0)
      touch(dir.path / (stem + ".log"));
  }
  touch(dir.path / "notes.txt");
  touch(dir.path / RunningMarkerName);

  const auto before = crashFiles(dir.path);
  REQUIRE(before.size() == 12);
  CHECK(before.front().filename().string() == "crash-20261108-120000-42.txt");

  pruneCrashFiles(dir.path);
  const auto after = crashFiles(dir.path);
  CHECK(after.size() == 5 + 3);
  CHECK(after.front().filename().string() == "crash-20261108-120000-42.txt");
  CHECK(fs::exists(dir.path / "crash-20261104-120000-42.txt"));
  CHECK(fs::exists(dir.path / "crash-20261104-120000-42.log"));
  CHECK_FALSE(fs::exists(dir.path / "crash-20261103-120000-42.txt"));
  CHECK_FALSE(fs::exists(dir.path / "crash-20261102-120000-42.log"));
  CHECK(fs::exists(dir.path / "notes.txt"));
  CHECK(fs::exists(dir.path / RunningMarkerName));

  // Nothing to do on a folder that is not there.
  pruneCrashFiles(dir.path / "missing");
  CHECK(crashFiles(dir.path / "missing").empty());
}

TEST_CASE("the report says when the previous session crashed, with the crash's text")
{
  ReportInput in;
  in.clientVersion = "0.1.0";
  {
    const auto entries = buildReport(in);
    CHECK(entries.at(0).data.find("Previous session: ended normally") != std::string::npos);
    for (const ZipEntry& entry : entries)
      CHECK(entry.name != "crash.txt");
  }
  in.previousSessionCrashed = true;
  in.previousSessionInfo = "version 0.0.9, started 2026-11-23T10:00:00";
  in.crashFiles = {"crash-20261123-100500-7.txt (4 KB)", "crash-20261123-100500-7.log (12 KB)"};
  in.crashText = "Signal: SIGSEGV\nopening https://media.example.org/Videos/1?api_key=0123456789abcdef0123456789abcdef\n";
  const auto entries = buildReport(in);
  const std::string& report = entries.at(0).data;
  CHECK(report.find("Previous session: ended unexpectedly (version 0.0.9") != std::string::npos);
  CHECK(report.find("crash-20261123-100500-7.txt (4 KB)") != std::string::npos);
  bool found = false;
  for (const ZipEntry& entry : entries) {
    if (entry.name != "crash.txt")
      continue;
    found = true;
    CHECK(entry.data.find("SIGSEGV") != std::string::npos);
    // Secrets are hidden here too.
    CHECK(entry.data.find("0123456789abcdef0123456789abcdef") == std::string::npos);
    CHECK(entry.data.find("media.example.org") == std::string::npos);
  }
  CHECK(found);
}

#ifndef _WIN32
TEST_CASE("a crash leaves its signal and a backtrace behind, and the process still dies of it")
{
  TempFolder dir;
  const fs::path crashes = dir.path / "crashes";
  const pid_t child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    // The child: no output of its own on the test's stderr.
    if (!installCrashHandler(crashes, "20261123-120000-1", "Jellyfin Desktop Next test crashed"))
      _exit(3);
    std::freopen("/dev/null", "w", stderr);
    std::raise(SIGSEGV);
    _exit(4);
  }
  int status = 0;
  REQUIRE(waitpid(child, &status, 0) == child);
  CHECK(WIFSIGNALED(status));
  if (WIFSIGNALED(status))
    CHECK(WTERMSIG(status) == SIGSEGV);
  const fs::path text = crashes / "crash-20261123-120000-1.txt";
  REQUIRE(fs::exists(text));
  const std::string written = readAll(text);
  CHECK(written.find("Jellyfin Desktop Next test crashed") == 0);
  CHECK(written.find("Signal: SIGSEGV") != std::string::npos);
  CHECK(written.find("Backtrace:") != std::string::npos);
  CHECK(crashFiles(crashes).size() == 1);
}
#endif
