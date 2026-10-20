#include <jfd/session/InputQueue.h>

#include <utility>

namespace jfd::session {

  InputQueue::InputQueue(Handler handler, QObject* parent)
      : QObject(parent), m_handler(std::move(handler)) {}

  void InputQueue::post(core::SessionInput input) {
    QMetaObject::invokeMethod(
        this, [this, input = std::move(input)]() mutable { m_handler(std::move(input)); },
        Qt::QueuedConnection);
  }

} // namespace jfd::session
