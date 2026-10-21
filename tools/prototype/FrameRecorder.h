#pragma once

#include <jfd/diagnostics/FrameStats.h>

#include <QObject>

#include <chrono>
#include <mutex>

class QQuickWindow;

// Records the time of every buffer swap of a Qt Quick window. Swaps are
// reported on the render thread; readers take copies under the lock.
class FrameRecorder : public QObject
{
  Q_OBJECT
public:
  explicit FrameRecorder(QQuickWindow* window, QObject* parent = nullptr);

  // Starts a new measurement: forgets everything recorded so far.
  void restart();
  // Recorded intervals since the last restart.
  jfd::diagnostics::FrameStats measurement() const;
  // Intervals since the previous call (for the on-screen statistics).
  jfd::diagnostics::FrameStats takeRecent();

  static std::int64_t nowUs()
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

private:
  void onSwapped();

  mutable std::mutex m_mutex;
  jfd::diagnostics::FrameStats m_measurement {60 * 60 * 30};
  jfd::diagnostics::FrameStats m_recent {512};
  std::int64_t m_lastRecent = -1;
};
