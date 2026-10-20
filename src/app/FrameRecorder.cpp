#include "FrameRecorder.h"

#include <QQuickWindow>

FrameRecorder::FrameRecorder(QQuickWindow* window, QObject* parent) : QObject(parent)
{
  connect(window, &QQuickWindow::frameSwapped, this, &FrameRecorder::onSwapped, Qt::DirectConnection);
}

void FrameRecorder::onSwapped()
{
  const auto now = nowUs();
  std::lock_guard lock(m_mutex);
  m_measurement.addFrame(now);
  m_recent.addFrame(now);
}

void FrameRecorder::restart()
{
  std::lock_guard lock(m_mutex);
  m_measurement.clear();
}

jfd::diagnostics::FrameStats FrameRecorder::measurement() const
{
  std::lock_guard lock(m_mutex);
  return m_measurement;
}

jfd::diagnostics::FrameStats FrameRecorder::takeRecent()
{
  std::lock_guard lock(m_mutex);
  auto result = m_recent;
  // Keep the last timestamp so the next interval spans the boundary.
  m_recent.clearIntervals();
  return result;
}
