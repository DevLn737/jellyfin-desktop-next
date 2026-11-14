#pragma once

#include <doctest/doctest.h>

#include <QtGlobal>

namespace jfd::test {

  // Whether the extras stand (scripts/test-server/extras-server.sh, which
  // exports JFD_EXTRAS_SERVER_URL) is up. Without it the extras cases are
  // skipped on a developer's machine. CI sets JFD_REQUIRE_EXTRAS=1 where it
  // starts the stand, so a stand that did not come up, or a script that
  // stopped exporting the URL, fails the case instead of passing silently.
  inline bool hasExtrasStand() {
    if (!qEnvironmentVariableIsEmpty("JFD_EXTRAS_SERVER_URL")) return true;
    if (qEnvironmentVariableIntValue("JFD_REQUIRE_EXTRAS") == 1)
      FAIL("JFD_REQUIRE_EXTRAS=1 but JFD_EXTRAS_SERVER_URL is not set: the extras stand is missing");
    MESSAGE("JFD_EXTRAS_SERVER_URL is not set (scripts/test-server/extras-server.sh up): skipped");
    return false;
  }

} // namespace jfd::test
