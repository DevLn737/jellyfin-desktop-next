#include <jfd/diagnostics/Report.h>
#include <jfd/diagnostics/Zip.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <map>
#include <string>

using namespace jfd::diagnostics;

namespace {

std::uint32_t read16(const std::string& s, std::size_t at)
{
  return static_cast<unsigned char>(s[at]) | (static_cast<unsigned char>(s[at + 1]) << 8);
}

std::uint32_t read32(const std::string& s, std::size_t at)
{
  return read16(s, at) | (read16(s, at + 2) << 16);
}

// Reads a stored-entry archive through its central directory, as unzip does.
std::map<std::string, std::string> readZip(const std::string& zip)
{
  std::map<std::string, std::string> files;
  REQUIRE(zip.size() >= 22);
  const std::size_t end = zip.size() - 22;
  REQUIRE(read32(zip, end) == 0x06054B50u);
  const std::uint32_t count = read16(zip, end + 10);
  std::size_t at = read32(zip, end + 16);
  for (std::uint32_t i = 0; i < count; ++i) {
    REQUIRE(read32(zip, at) == 0x02014B50u);
    const std::uint32_t crc = read32(zip, at + 16);
    const std::uint32_t size = read32(zip, at + 24);
    const std::uint32_t nameLength = read16(zip, at + 28);
    const std::uint32_t local = read32(zip, at + 42);
    const std::string name = zip.substr(at + 46, nameLength);
    REQUIRE(read32(zip, local) == 0x04034B50u);
    const std::size_t dataAt = local + 30 + read16(zip, local + 26) + read16(zip, local + 28);
    const std::string data = zip.substr(dataAt, size);
    CHECK(crc32(data) == crc);
    files[name] = data;
    at += 46 + nameLength;
  }
  return files;
}

constexpr const char* Secret = "0123456789abcdef0123456789abcdef";

ReportInput planted()
{
  const std::string s = Secret;
  ReportInput in;
  in.createdAt = "2026-10-25 19:30:00 UTC";
  in.clientVersion = "0.1.0";
  in.qtVersion = "6.12.0";
  in.osName = "Windows 11 IoT Enterprise LTSC";
  in.cpuArchitecture = "x86_64";
  in.gpu = "Direct3D 11, AMD Radeon RX 9060 XT";
  in.mpvVersion = "mpv 0.40.0";
  in.hwdecSetting = "auto-safe";
  in.hwdecCurrent = "d3d11va";
  in.serverVersion = "12.2.0";
  in.settings = {{"interface/language", "ru"}, {"playback/hardwareDecoding", "auto-safe"}};
  in.log = "2026-10-25 19:00:00.000 info [jfd.app] started\n"
           "2026-10-25 19:00:01.000 warning [default] mpv ffmpeg http: failed https://media.example.org/Videos/x/"
           "master.m3u8?ApiKey=" + s + "\n"
           "Authorization: MediaBrowser Client=\"Jellyfin Desktop\", Token=\"" + s + "\"\n";
  in.previousLog = "ws://192.168.1.20:8096/socket?api_key=" + s + "&deviceId=d\n";
  in.sessionJournal = {"0.000 in play item=abc", "1.250 out unpause x-emby-token: " + s};
  in.statsSnapshots = {"process rss=412 MB cpu=9.1%", "frames n=300 p95=16.9 ms"};
  return in;
}

}

TEST_CASE("the CRC-32 matches the zip format's check value")
{
  CHECK(crc32("123456789") == 0xCBF43926u);
  CHECK(crc32("") == 0u);
}

TEST_CASE("a zip can be read back entry by entry")
{
  const std::vector<ZipEntry> entries = {{"a.txt", "hello\n"}, {"dir/b.txt", std::string(100000, 'x')}, {"empty", ""}};
  const auto files = readZip(makeZip(entries, 0x5A99, 0x9800));
  REQUIRE(files.size() == 3);
  CHECK(files.at("a.txt") == "hello\n");
  CHECK(files.at("dir/b.txt").size() == 100000);
  CHECK(files.at("empty").empty());
}

TEST_CASE("the report has the parts a bug report needs")
{
  const auto files = readZip(makeZip(buildReport(planted()), 0, 0));
  REQUIRE(files.count("report.txt") == 1);
  REQUIRE(files.count("log.txt") == 1);
  REQUIRE(files.count("log-previous.txt") == 1);
  REQUIRE(files.count("session-journal.txt") == 1);
  REQUIRE(files.count("stats.txt") == 1);
  const std::string& report = files.at("report.txt");
  CHECK(report.find("Client: 0.1.0") != std::string::npos);
  CHECK(report.find("AMD Radeon RX 9060 XT") != std::string::npos);
  CHECK(report.find("in use d3d11va") != std::string::npos);
  CHECK(report.find("Jellyfin 12.2.0") != std::string::npos);
  CHECK(report.find("playback/hardwareDecoding = auto-safe") != std::string::npos);
  CHECK(files.at("session-journal.txt").find("in play item=abc") != std::string::npos);
  CHECK(files.at("stats.txt").find("p95=16.9 ms") != std::string::npos);
}

TEST_CASE("the report holds no secret from any source")
{
  const auto files = readZip(makeZip(buildReport(planted()), 0, 0));
  for (const auto& [name, data] : files) {
    CAPTURE(name);
    CHECK(data.find(Secret) == std::string::npos);
    CHECK(data.find("media.example.org") == std::string::npos);
    CHECK(data.find("192.168.1.20") == std::string::npos);
  }
  // The rest of the line stays readable.
  CHECK(files.at("log.txt").find("master.m3u8?ApiKey=***") != std::string::npos);
  CHECK(files.at("log.txt").find("Client=\"Jellyfin Desktop\"") != std::string::npos);
}

TEST_CASE("a huge log is cut to its end and the archive stays under the limit")
{
  ReportInput in = planted();
  std::string big;
  for (int i = 0; big.size() < 40u * 1024 * 1024; ++i)
    big += "line " + std::to_string(i) + " some log text to fill the file up\n";
  in.log = big;
  in.previousLog = big;
  in.sessionJournal.assign(500000, "1.000 in tick");
  const std::string zip = makeZip(buildReport(in), 0, 0);
  CHECK(zip.size() <= MaxReportBytes);
  const auto files = readZip(zip);
  const std::string& log = files.at("log.txt");
  CHECK(log.find("[earlier lines cut") == 0);
  // The end of the log is what is kept.
  const std::string last = big.substr(big.rfind("line "), big.size());
  CHECK(log.find(last.substr(0, last.find('\n'))) != std::string::npos);
}

TEST_CASE("missing facts read as unknown, not as blanks")
{
  const auto files = readZip(makeZip(buildReport(ReportInput{}), 0, 0));
  CHECK(files.at("report.txt").find("Graphics: unknown") != std::string::npos);
  CHECK(files.at("report.txt").find("Server: Jellyfin unknown") != std::string::npos);
}
