#include <jfd/diagnostics/LogFile.h>

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

using namespace jfd::diagnostics;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  TempDir()
  {
    std::random_device random;
    path = fs::temp_directory_path() / ("jfd-logfile-" + std::to_string(random()));
    fs::create_directories(path);
  }
  ~TempDir()
  {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

std::string contents(const fs::path& file)
{
  std::ifstream in(file);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

}

TEST_CASE("the log keeps the previous run's file and moves itself aside past its limit")
{
  TempDir dir;
  const fs::path current = dir.path / "app.log";
  const fs::path previous = dir.path / "app.old.log";
  {
    std::ofstream(current) << "last run\n";
    std::ofstream(previous) << "the run before\n";
  }
  LogFile::Options options;
  options.maxBytes = 1000;
  LogFile log(options);
  REQUIRE(log.open(current, previous));
  // The previous run's log is the old one now; the one before is gone.
  CHECK(contents(previous) == "last run\n");

  const std::string line(99, 'x');
  for (int i = 0; i < 25; ++i)
    log.write(line + "\n", false);
  log.flush();
  // 2500 bytes through a 1000 byte limit: moved aside twice, never over it.
  CHECK(log.rotations() == 2);
  CHECK(log.size() <= options.maxBytes);
  CHECK(fs::file_size(current) <= options.maxBytes + 100);
  CHECK(fs::file_size(previous) <= options.maxBytes + 100);
  CHECK(contents(previous).find("last run") == std::string::npos);
  // Only two files, whatever was written.
  int files = 0;
  for ([[maybe_unused]] const auto& entry : fs::directory_iterator(dir.path))
    ++files;
  CHECK(files == 2);
}

TEST_CASE("a warning is on disk at once, other lines within the flush interval")
{
  TempDir dir;
  const fs::path current = dir.path / "app.log";
  LogFile log;
  REQUIRE(log.open(current, dir.path / "app.old.log"));
  const auto start = LogFile::Clock::now();

  log.write("debug line\n", false, start);
  // Buffered: not on disk yet.
  CHECK(contents(current).empty());
  log.flushIfDue(start + std::chrono::milliseconds(500));
  CHECK(contents(current).empty());
  log.flushIfDue(start + std::chrono::milliseconds(1000));
  CHECK(contents(current) == "debug line\n");

  log.write("info line\n", false, start + std::chrono::milliseconds(1100));
  CHECK(contents(current) == "debug line\n");
  // A warning takes the lines before it along.
  log.write("warning line\n", true, start + std::chrono::milliseconds(1200));
  CHECK(contents(current) == "debug line\ninfo line\nwarning line\n");

  // A line written long after the last flush goes out with itself.
  log.write("late line\n", false, start + std::chrono::milliseconds(1300));
  log.write("later line\n", false, start + std::chrono::milliseconds(2400));
  CHECK(contents(current).find("later line") != std::string::npos);

  log.write("closing\n", false);
  log.close();
  CHECK(contents(current).find("closing") != std::string::npos);
}
