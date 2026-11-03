#include <jfd/diagnostics/Redact.h>

#include <doctest/doctest.h>

#include <initializer_list>
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

TEST_CASE("Seerr secrets: API key header, cookies and addresses without a scheme are hidden")
{
  const std::string s = Secret;
  for (const std::string& line : {
           "X-Api-Key: " + s,
           "x-api-key=" + s,
           "GET /api/v1/settings/main x-api-key: " + s + " 200",
           "Cookie: connect.sid=" + s + "; theme=dark",
           "Set-Cookie: connect.sid=s%3A" + s + ".sig; Path=/; HttpOnly",
           "set-cookie: other=1; connect.sid=" + s,
           "{\"connect.sid\":\"" + s + "\"}",
           "connect.sid=" + s,
       }) {
    CAPTURE(line);
    CHECK_FALSE(leaks(redactSecrets(line)));
  }
  CHECK(redactSecrets("Cookie: a=1; b=2\r\nHost: x") == "Cookie: ***\r\nHost: x");
  CHECK(redactSecrets("cookies: 3") == "cookies: 3");

  CHECK(redactSecrets("connect to 192.168.1.50:5055 failed") == "connect to *** failed");
  CHECK(redactSecrets("seerr.example.org:5055/api/v1") == "***/api/v1");
  CHECK(redactSecrets("proxy=Seerr.Home.Lan:443,") == "proxy=***,");
  CHECK(redactSecrets("http://10.0.0.5:5055/api") == "http://***/api");
  CHECK(redactSecrets("127.0.0.1:18096 up") == "127.0.0.1:18096 up");
  CHECK(redactSecrets("localhost:5055") == "localhost:5055");
  CHECK(redactSecrets("Redact.cpp:42: failed") == "Redact.cpp:42: failed");
  CHECK(redactSecrets("movie.mkv:12 at 00:01:02") == "movie.mkv:12 at 00:01:02");
  CHECK(redactSecrets("version 0.38.0.1 ratio 16:9") == "version 0.38.0.1 ratio 16:9");
}

TEST_CASE("configured addresses are hidden wherever they appear")
{
  clearProtectedAddresses();
  addProtectedAddress("https://Seerr.Example.Org/");
  addProtectedAddress("media.lan");
  addProtectedAddress("localhost:5055");
  addProtectedAddress("");
  CHECK(redactSecrets("redirect to seerr.example.org/login") == "redirect to ***/login");
  CHECK(redactSecrets("Host: SEERR.EXAMPLE.ORG") == "Host: ***");
  CHECK(redactSecrets("resolve media.lan failed") == "resolve *** failed");
  CHECK(redactSecrets("localhost:5055") == "localhost:5055");
  CHECK(redactSecrets("nothing here") == "nothing here");
  clearProtectedAddresses();
  CHECK(redactSecrets("resolve media.lan failed") == "resolve media.lan failed");
}

TEST_CASE("the Quick Connect secret and a pw field in a query are hidden")
{
  const std::string s = Secret;
  for (const std::string& line : std::initializer_list<std::string>{
           "POST /QuickConnect/Connect?secret=" + s + "&userId=u",
           "GET /QuickConnect/Connect?Secret=" + s,
           "{\"Secret\":\"" + s + "\",\"Code\":\"123456\"}",
           "{\"Authenticated\":true,\"Secret\": \"" + s + "\"}",
           "Username=a&Pw=" + s + "&x=1",
           "pw: " + s,
           "client_secret=" + s,
       }) {
    CAPTURE(line);
    CHECK_FALSE(leaks(redactSecrets(line)));
  }
  CHECK(redactSecrets("Username=a&Pw=" + s + "&x=1") == "Username=a&Pw=***&x=1");
  CHECK(redactSecrets("GET /QuickConnect/Connect?secret=" + s + "&userId=u") ==
        "GET /QuickConnect/Connect?secret=***&userId=u");
  // Only at the start of a word.
  CHECK(redactSecrets("rpw=3 nosecret=4 cpw: 5") == "rpw=3 nosecret=4 cpw: 5");
  CHECK(redactSecrets("Secret Service: unlocked") == "Secret Service: unlocked");
}

TEST_CASE("percent-encoded addresses and secrets are hidden too")
{
  const std::string s = Secret;
  for (const std::string& line : std::initializer_list<std::string>{
           std::string("image://jfd/https%3A%2F%2Fjelly.example.org%2FItems%2Fa%2FImages%2FPrimary%3Ftag%3Dx"),
           "redirect=https%3A%2F%2F10.0.0.5%3A8920%2Fweb",
           "next?token%3D" + s + "%26x%3D1",
           "u=%68ttps%3A%2F%2Fjelly.example.org",
           // Encoded twice: an encoded URL as a parameter of another one.
           "image://jfd/https%253A%252F%252Fjelly.example.org%252FItems%253Fapi_key%253D" + s,
       }) {
    CAPTURE(line);
    const std::string out = redactSecrets(line);
    CAPTURE(out);
    CHECK_FALSE(leaks(out));
    CHECK(out.find("jelly.example.org") == std::string::npos);
    CHECK(out.find("10.0.0.5") == std::string::npos);
    CHECK(out.find("***") != std::string::npos);
  }
  CHECK(redactSecrets("image://jfd/https%3A%2F%2Fjelly.example.org%2FItems%2Fa") ==
        "image://jfd/https%3A%2F%2F***%2FItems%2Fa");
  CHECK(redactSecrets("next?token%3D" + s + "%26x%3D1") == "next?token%3D***%26x%3D1");
  // This computer's addresses and plain percentages stay as they are.
  CHECK(redactSecrets("image://jfd/http%3A%2F%2F127.0.0.1%3A8096%2FItems") ==
        "image://jfd/http%3A%2F%2F127.0.0.1%3A8096%2FItems");
  CHECK(redactSecrets("buffered 50% of 100%") == "buffered 50% of 100%");
  CHECK(redactSecrets("name=a%20b%zz") == "name=a%20b%zz");
}
