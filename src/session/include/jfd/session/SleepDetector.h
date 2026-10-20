#pragma once

#include <QObject>
#include <QTimer>

#include <chrono>
#include <functional>

namespace jfd::session {

  // Notices that the computer slept: a short repeating timer whose ticks
  // arrive far apart on the wall clock. Works the same on every platform
  // (the steady clock stops during suspend on Linux but not on Windows; the
  // wall clock moves on both). A large jump of the system clock reads as a
  // sleep too, which only costs a reconnect.
  class SleepDetector final : public QObject {
    Q_OBJECT

  public:
    using WallClock = std::function<std::chrono::system_clock::time_point()>;

    explicit SleepDetector(QObject* parent = nullptr, WallClock now = &std::chrono::system_clock::now);

    static constexpr std::chrono::milliseconds Interval{2000};
    // A gap this much longer than the interval counts as a sleep.
    static constexpr std::chrono::milliseconds Threshold{10000};

    // One tick; the timer calls it, tests may too.
    void check();

  Q_SIGNALS:
    void resumed(qint64 sleptMs);

  private:
    WallClock m_now;
    std::chrono::system_clock::time_point m_last;
    QTimer m_timer;
  };

} // namespace jfd::session
