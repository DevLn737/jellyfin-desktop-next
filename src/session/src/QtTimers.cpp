#include <jfd/session/QtTimers.h>

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

namespace jfd::session {

  QtTimers::QtTimers(const core::Clock& clock, Fired fired, QObject* parent)
      : QObject(parent), m_clock(clock), m_fired(std::move(fired)) {}

  void QtTimers::arm(core::TimerId id, core::LocalInstant due) {
    cancel(id);
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(due - m_clock.now());
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, [this, id, timer] {
      m_timers.erase(id.serial);
      timer->deleteLater();
      m_fired(core::TimerFired{id});
    });
    m_timers[id.serial] = timer;
    timer->start(std::max<std::chrono::milliseconds>(wait, std::chrono::milliseconds(0)));
  }

  void QtTimers::cancel(core::TimerId id) {
    const auto it = m_timers.find(id.serial);
    if (it == m_timers.end()) return;
    it->second->stop();
    it->second->deleteLater();
    m_timers.erase(it);
  }

} // namespace jfd::session
