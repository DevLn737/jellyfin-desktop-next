#pragma once

#include <QDeadlineTimer>
#include <QEventLoop>
#include <QTimer>

namespace jfd::test {

  // Runs the event loop until `done` returns true or the timeout passes.
  template <class Predicate> bool spinUntil(Predicate done, int timeoutMs = 5000) {
    QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
      if (deadline.hasExpired()) return false;
      QEventLoop loop;
      QTimer::singleShot(5, &loop, &QEventLoop::quit);
      loop.exec();
    }
    return true;
  }

} // namespace jfd::test
