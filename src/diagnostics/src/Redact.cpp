#include <jfd/diagnostics/Redact.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>
#include <vector>

namespace jfd::diagnostics {

namespace {

// Lowercase names; each is followed by = or : (with optional quotes and
// spaces), then the secret. "token" also covers X-Emby-Token, AccessToken
// and the Token="..." field of a MediaBrowser Authorization header.
constexpr std::array<std::string_view, 6> SecretNames = {
    "api_key", "apikey", "token", "password", "\"pw", "authorization"};

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
      while (end < folded.size() && !endsValue(folded[end]))
        ++end;
      if (end > at)
        spans.emplace_back(at, end);
    }
  }
  addServerAddresses(folded, spans);
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
