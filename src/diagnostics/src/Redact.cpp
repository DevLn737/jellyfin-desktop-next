#include <jfd/diagnostics/Redact.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <utility>
#include <vector>

namespace jfd::diagnostics {

namespace {

// Lowercase names; each is followed by = or : (with optional quotes and
// spaces), then the secret. "token" also covers X-Emby-Token, AccessToken
// and the Token="..." field of a MediaBrowser Authorization header;
// "api-key" covers X-Api-Key; "cookie" covers Cookie and Set-Cookie, whose
// whole value (every cookie on the line) is hidden.
constexpr std::array<std::string_view, 9> SecretNames = {
    "api_key", "api-key", "apikey", "token", "password", "\"pw", "authorization", "cookie",
    "connect.sid"};

std::mutex ProtectedMutex;
std::vector<std::string> ProtectedHosts;

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool isSpace(char c) { return c == ' ' || c == '\t'; }

bool endsValue(char c)
{
  switch (c) {
  case '&': case '"': case '\'': case ',': case ';': case ')': case '(': case '<': case '>':
  case ']': case '[': case '}': case '{': case ' ': case '\t': case '\r': case '\n':
    return true;
  default:
    return false;
  }
}

bool startsWithWord(std::string_view text, std::size_t at, std::string_view word)
{
  if (at + word.size() > text.size())
    return false;
  for (std::size_t i = 0; i < word.size(); ++i) {
    if (text[at + i] != word[i])
      return false;
  }
  return true;
}

bool isSchemeChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'; }

// The host part of a URL ends at the path, query or fragment, or where the
// value ends.
bool endsAuthority(char c) { return c == '/' || c == '?' || c == '#' || c == '\\' || endsValue(c); }

bool isLoopback(std::string_view host)
{
  const auto at = host.rfind('@');
  if (at != std::string_view::npos)
    host = host.substr(at + 1);
  return host.starts_with("localhost") || host.starts_with("127.") || host.starts_with("[::1]");
}

// Server addresses: the authority of http, https, ws and wss URLs, except
// this computer's own.
void addServerAddresses(std::string_view folded, std::vector<std::pair<std::size_t, std::size_t>>& spans)
{
  for (std::size_t found = folded.find("://"); found != std::string_view::npos;
       found = folded.find("://", found + 3)) {
    std::size_t schemeStart = found;
    while (schemeStart > 0 && isSchemeChar(folded[schemeStart - 1]))
      --schemeStart;
    const std::string_view scheme = folded.substr(schemeStart, found - schemeStart);
    if (scheme != "http" && scheme != "https" && scheme != "ws" && scheme != "wss")
      continue;
    const std::size_t start = found + 3;
    std::size_t end = start;
    while (end < folded.size() && !endsAuthority(folded[end]))
      ++end;
    if (end > start && !isLoopback(folded.substr(start, end - start)))
      spans.emplace_back(start, end);
  }
}


bool isHostChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'; }

// Source files and media names that look like host:port in a log line.
bool isFileExtension(std::string_view label)
{
  for (const std::string_view ext : {"cpp", "h", "hpp", "c", "cc", "qml", "js", "mjs", "json", "log", "txt",
                                     "xml", "mkv", "mp4", "avi", "mov", "ts", "srt", "ass", "vtt", "mp3",
                                     "flac", "py", "sh", "yml", "yaml", "conf", "ini", "so", "dll", "exe"}) {
    if (label == ext)
      return true;
  }
  return false;
}

// Addresses written without a scheme: a dotted host name or IPv4 address
// followed by a port (192.168.1.50:5055, seerr.example.org:5055), except
// this computer's own and file names with a line number.
void addBareAddresses(std::string_view folded, std::vector<std::pair<std::size_t, std::size_t>>& spans)
{
  for (std::size_t colon = folded.find(':'); colon != std::string_view::npos;
       colon = folded.find(':', colon + 1)) {
    std::size_t port = colon + 1;
    while (port < folded.size() && folded[port] >= '0' && folded[port] <= '9')
      ++port;
    const std::size_t digits = port - colon - 1;
    if (digits < 2 || digits > 5 || (port < folded.size() && isHostChar(folded[port]) && folded[port] != '.'))
      continue;
    std::size_t start = colon;
    while (start > 0 && isHostChar(folded[start - 1]))
      --start;
    const std::string_view host = folded.substr(start, colon - start);
    const std::size_t dot = host.rfind('.');
    if (host.empty() || dot == std::string_view::npos || dot + 1 == host.size() || host.front() == '.' ||
        host.front() == '-')
      continue;
    if (start > 0 && folded[start - 1] == '/')
      continue;  // part of a path or a scheme address, handled there
    if (isLoopback(host) || isFileExtension(host.substr(dot + 1)))
      continue;
    spans.emplace_back(start, port);
  }
}

// Addresses the user configured (a Seerr server, say), wherever they appear
// and in whatever case; with or without a scheme or port as configured.
void addProtectedHosts(std::string_view folded, std::vector<std::pair<std::size_t, std::size_t>>& spans)
{
  const std::lock_guard lock(ProtectedMutex);
  for (const std::string& host : ProtectedHosts) {
    for (std::size_t found = folded.find(host); found != std::string_view::npos;
         found = folded.find(host, found + host.size()))
      spans.emplace_back(found, found + host.size());
  }
}

}

void addProtectedAddress(std::string_view address)
{
  std::string host(address);
  std::transform(host.begin(), host.end(), host.begin(), lower);
  const std::size_t scheme = host.find("://");
  if (scheme != std::string::npos)
    host.erase(0, scheme + 3);
  const std::size_t end = host.find_first_of("/?#\\ \t");
  if (end != std::string::npos)
    host.erase(end);
  const std::size_t at = host.rfind('@');
  if (at != std::string::npos)
    host.erase(0, at + 1);
  if (host.size() < 3 || isLoopback(host))
    return;
  const std::lock_guard lock(ProtectedMutex);
  if (std::find(ProtectedHosts.begin(), ProtectedHosts.end(), host) == ProtectedHosts.end())
    ProtectedHosts.push_back(std::move(host));
}

void clearProtectedAddresses()
{
  const std::lock_guard lock(ProtectedMutex);
  ProtectedHosts.clear();
}

std::string redactSecrets(std::string_view line)
{
  std::string folded(line);
  std::transform(folded.begin(), folded.end(), folded.begin(), lower);

  std::vector<std::pair<std::size_t, std::size_t>> spans;
  for (const std::string_view name : SecretNames) {
    for (std::size_t found = folded.find(name); found != std::string::npos;
         found = folded.find(name, found + 1)) {
      std::size_t at = found + name.size();
      if (at < folded.size() && folded[at] == '"')
        ++at;
      while (at < folded.size() && isSpace(folded[at]))
        ++at;
      if (at >= folded.size() || (folded[at] != '=' && folded[at] != ':'))
        continue;
      ++at;
      while (at < folded.size() && isSpace(folded[at]))
        ++at;
      if (name == "authorization") {
        // Only schemes whose whole value is the credential; a MediaBrowser
        // header carries its key in Token=, matched on its own.
        bool scheme = false;
        for (const std::string_view word : {std::string_view("bearer"), std::string_view("basic")}) {
          if (startsWithWord(folded, at, word) && at + word.size() < folded.size() &&
              isSpace(folded[at + word.size()])) {
            at += word.size();
            scheme = true;
            break;
          }
        }
        if (!scheme)
          continue;
        while (at < folded.size() && isSpace(folded[at]))
          ++at;
      } else if (at < folded.size() && (folded[at] == '"' || folded[at] == '\'')) {
        ++at;
      }
      std::size_t end = at;
      if (name == "cookie") {
        while (end < folded.size() && folded[end] != '\r' && folded[end] != '\n' && folded[end] != '"')
          ++end;
      } else {
        while (end < folded.size() && !endsValue(folded[end]))
          ++end;
      }
      if (end > at)
        spans.emplace_back(at, end);
    }
  }
  addServerAddresses(folded, spans);
  addBareAddresses(folded, spans);
  addProtectedHosts(folded, spans);
  if (spans.empty())
    return std::string(line);

  std::sort(spans.begin(), spans.end());
  std::string out;
  out.reserve(line.size());
  std::size_t copied = 0;
  for (const auto& [start, end] : spans) {
    if (end <= copied)
      continue;
    const std::size_t from = std::max(start, copied);
    out.append(line.substr(copied, from - copied));
    if (from == start)
      out.append("***");
    copied = end;
  }
  out.append(line.substr(copied));
  return out;
}

}
