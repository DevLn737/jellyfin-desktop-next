#pragma once

#include <QString>

#include <compare>
#include <optional>

namespace jfd::api {

  struct ServerVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;

    // Parses "12.2.0" and tolerates a fourth component or a suffix such as
    // "12.2.0-syncplay.1".
    static std::optional<ServerVersion> parse(QStringView text);
    QString toString() const;

    constexpr auto operator<=>(const ServerVersion&) const = default;
  };

  // The oldest server this client talks to. 10.10 is the first release with
  // every route used here (UserItems, UserPlayedItems, POST QuickConnect/Initiate).
  inline constexpr ServerVersion MinimumServerVersion{10, 10, 0};

} // namespace jfd::api
