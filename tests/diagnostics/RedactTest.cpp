#include <jfd/diagnostics/Redact.h>

#include <doctest/doctest.h>

#include <string>

using namespace jfd::diagnostics;

namespace {

constexpr const char* Secret = "0123456789abcdef0123456789abcdef";

bool leaks(const std::string& text) { return text.find(Secret) != std::string::npos; }

}

TEST_CASE("access keys in stream and socket addresses are hidden")
{
  const std::string s = Secret;
  for (const std::string& line : {
           "[ffmpeg] http: HTTP error 404 Not Found for http://10.0.0.5:8096/Videos/abc/master.m3u8?"
           "MediaSourceId=abc&ApiKey=" + s + "&PlaySessionId=42",
           "open http://host/Videos/abc/stream?api_key=" + s,
           "ws://host:8096/socket?api_key=" + s + "&deviceId=dev",
           "subtitle: https://host/Videos/a/b/Subtitles/2/0/Stream.srt?APIKEY=" + s + " failed",
           "url \"https://host/Items/1/Images/Primary?apikey=" + s + "\"",
       }) {
    CAPTURE(line);
    const std::string out = redactSecrets(line);
    CHECK_FALSE(leaks(out));
    CHECK(out.find("***") != std::string::npos);
  }
  CHECK(redactSecrets("GET /Videos/abc/stream?api_key=" + s + "&Static=true") ==
        "GET /Videos/abc/stream?api_key=***&Static=true");
}

TEST_CASE("tokens in headers, JSON and authorization schemes are hidden")
{
  const std::string s = Secret;
  for (const std::string& line : {
           "Authorization: MediaBrowser Client=\"Jellyfin Desktop\", Device=\"PC\", DeviceId=\"d\", "
           "Version=\"0.1\", Token=\"" + s + "\"",
           "X-Emby-Token: " + s,
           "x-mediabrowser-token=" + s,
           "{\"User\":{\"Name\":\"a\"},\"AccessToken\":\"" + s + "\",\"ServerId\":\"x\"}",
           "{\"AccessToken\" : \"" + s + "\"}",
           "Authorization: Bearer " + s,
           "authorization=basic " + s,
           "{\"Username\":\"a\",\"Pw\":\"" + s + "\"}",
           "password=" + s,
       }) {
    CAPTURE(line);
    CHECK_FALSE(leaks(redactSecrets(line)));
  }
  CHECK(redactSecrets("Token=\"" + s + "\", Version=\"1\"") == "Token=\"***\", Version=\"1\"");
}

TEST_CASE("lines without secrets are unchanged")
{
  for (const std::string line : {
           "",
           "session: playing item 14401f7e4f9a2da047069ad4a3124f9c at 12.5 s",
           "Authorization: MediaBrowser Client=\"Jellyfin Desktop\"",
           "tokens left: 3",
           "token",
           "ApiKey=",
           "SyncPlay group a1b2 joined",
       }) {
    CAPTURE(line);
    CHECK(redactSecrets(line) == line);
  }
}

TEST_CASE("several secrets on one line are all hidden")
{
  const std::string s = Secret;
  const std::string out =
      redactSecrets("a?api_key=" + s + "&x=1 Token=\"" + s + "\" Authorization: Bearer " + s);
  CHECK_FALSE(leaks(out));
  CHECK(out == "a?api_key=***&x=1 Token=\"***\" Authorization: Bearer ***");
}

TEST_CASE("server addresses are hidden, this computer's are kept")
{
  CHECK(redactSecrets("GET https://jellyfin.example.org:8920/Users/Me failed") ==
        "GET https://***/Users/Me failed");
  CHECK(redactSecrets("ws://192.168.1.20:8096/socket?api_key=k&deviceId=d") ==
        "ws://***/socket?api_key=***&deviceId=d");
  CHECK(redactSecrets("url=\"http://user:pass@10.0.0.5\"") == "url=\"http://***\"");
  CHECK(redactSecrets("HTTP://Media.Home.Lan?x") == "HTTP://***?x");
  CHECK(redactSecrets("http://127.0.0.1:18096/System/Info") == "http://127.0.0.1:18096/System/Info");
  CHECK(redactSecrets("http://localhost:8096/") == "http://localhost:8096/");
  CHECK(redactSecrets("file:///home/user/video.mkv") == "file:///home/user/video.mkv");
  CHECK(redactSecrets("ratio 16:9 at 00:01:02") == "ratio 16:9 at 00:01:02");
}
