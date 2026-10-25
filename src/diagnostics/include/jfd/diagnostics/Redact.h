#pragma once

#include <string>
#include <string_view>

namespace jfd::diagnostics {

// Replaces server access keys and passwords in a log line with "***":
// api_key / ApiKey / Token query parameters and their JSON or header forms
// (X-Emby-Token, AccessToken, Token="..." inside a MediaBrowser
// Authorization header), Bearer and Basic credentials, passwords, and the
// server part of http(s) and ws(s) addresses (loopback addresses stay).
// Names match without regard to case. Paths, item ids and the rest of the
// line are unchanged, so the log stays readable.
std::string redactSecrets(std::string_view line);

}
