#include "../support/DoctestQt.h"

#include <jfd/session/SleepDetector.h>

#include <QSignalSpy>

using jfd::session::SleepDetector;
using namespace std::chrono_literals;

TEST_CASE("A long gap between ticks on the wall clock reads as a sleep") {
  auto now = std::chrono::system_clock::time_point{} + 1000h;
  SleepDetector detector(nullptr, [&now] { return now; });
  QSignalSpy resumed(&detector, &SleepDetector::resumed);

  // Ticks on time, and late ones short of the threshold, are not.
  now += SleepDetector::Interval;
  detector.check();
  now += SleepDetector::Interval + SleepDetector::Threshold - 1ms;
  detector.check();
  CHECK(resumed.count() == 0);

  now += SleepDetector::Interval + 30s;
  detector.check();
  REQUIRE(resumed.count() == 1);
  CHECK(resumed.front().front().toLongLong() == 30'000);

  // The next tick on time starts over.
  now += SleepDetector::Interval;
  detector.check();
  CHECK(resumed.count() == 1);
}
