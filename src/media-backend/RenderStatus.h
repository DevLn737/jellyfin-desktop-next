#pragma once

#include <QMutex>
#include <QMutexLocker>
#include <QString>

namespace jfd::media {

// What the render thread reported about a video item's render context, kept
// so the GUI thread can ask later: with the non-threaded render loop (for
// example software OpenGL over Remote Desktop) the first frame, and with it
// the render context, can be ready before anyone connected to the signal.
class RenderStatus
{
public:
  void setReady(const QString& renderer)
  {
    QMutexLocker lock(&m_mutex);
    m_renderer = renderer;
  }
  void setFailed(const QString& message)
  {
    QMutexLocker lock(&m_mutex);
    m_failure = message;
  }
  QString renderer() const
  {
    QMutexLocker lock(&m_mutex);
    return m_renderer;
  }
  QString failure() const
  {
    QMutexLocker lock(&m_mutex);
    return m_failure;
  }

private:
  mutable QMutex m_mutex;
  QString m_renderer;
  QString m_failure;
};

}
