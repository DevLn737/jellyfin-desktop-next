#pragma once

#include <jfd/core/SessionInput.h>
#include <jfd/core/Timers.h>

#include <QObject>

#include <functional>
#include <map>

class QTimer;

namespace jfd::session {

  // core::Timers on precise single-shot QTimers. A due timer is posted as
  // TimerFired; a cancelled one may still be in the queue, which the session
  // drops on entry.
  class QtTimers final : public QObject, public core::Timers {
    Q_OBJECT

  public:
    using Fired = std::function<void(core::TimerFired)>;

    QtTimers(const core::Clock& clock, Fired fired, QObject* parent = nullptr);

    void arm(core::TimerId id, core::LocalInstant due) override;
    void cancel(core::TimerId id) override;

    std::size_t armedCount() const { return m_timers.size(); }

  private:
    const core::Clock& m_clock;
    Fired m_fired;
    std::map<std::uint64_t, QTimer*> m_timers;
  };

} // namespace jfd::session
