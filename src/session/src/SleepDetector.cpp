#include <jfd/session/SleepDetector.h>

#include <utility>

namespace jfd::session {

  SleepDetector::SleepDetector(QObject* parent, WallClock now)
      : QObject(parent), m_now(std::move(now)), m_last(m_now()) {
    m_timer.setInterval(Interval);
    connect(&m_timer, &QTimer::timeout, this, &SleepDetector::check);
    m_timer.start();
  }

  void SleepDetector::check() {
    const auto now = m_now();
    const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last);
    m_last = now;
    if (gap > Interval + Threshold) Q_EMIT resumed(static_cast<qint64>((gap - Interval).count()));
  }

} // namespace jfd::session
