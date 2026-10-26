#pragma once

#include <string>
#include <string_view>

namespace jfd::diagnostics {

// Replaces server access keys and passwords in a log line with "***":
// api_key / ApiKey / Token query parameters and their JSON or header forms
// (X-Emby-Token, AccessToken, Token="..." inside a MediaBrowser
// Authorization header), Bearer and Basic credentials, passwords, and the
// server part of http(s) and ws(s) addresses (loopback addresses stay).
// Also hidden: X-Api-Key, the whole value of Cookie / Set-Cookie headers
// (connect.sid among them) and addresses written without a scheme but with
// a port (192.168.1.50:5055, seerr.example.org:5055). Names match without
// regard to case. Paths, item ids and the rest of the
// line are unchanged, so the log stays readable.
std::string redactSecrets(std::string_view line);

// An address the user configured (a URL or host[:port]): every later line
// shows *** in its place, in any case. Loopback addresses are ignored.
void addProtectedAddress(std::string_view address);
void clearProtectedAddresses();

}
