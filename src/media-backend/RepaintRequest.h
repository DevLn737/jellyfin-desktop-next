#pragma once

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>

#include <atomic>
#include <memory>

namespace jfd::media {

// mpv's update callback may come from any thread; it only queues a coalesced
// repaint request for the item on the GUI thread.
//
// The request is deleted with its renderer or node on the render thread,
// possibly while a queued repaint is still on its way, and the item may be
// destroyed on the GUI thread at any time. So the queued call owns what it
// touches (a shared state, never the request) and reads the item's QPointer
// only on the GUI thread.
class RepaintRequest
{
public:
  // Render thread, while the GUI thread is blocked in sync.
  explicit RepaintRequest(QQuickItem* item) : m_state(std::make_shared<State>(item)) {}

  // Any thread.
  void request()
  {
    if (m_state->pending.exchange(true))
      return;
    QCoreApplication* app = QCoreApplication::instance();
    if (!app)
      return;
    QMetaObject::invokeMethod(app, [state = m_state] {
      state->pending.store(false);
      if (state->item)
        state->item->update();
    }, Qt::QueuedConnection);
  }

  // For tests: whether a repaint is queued and not yet delivered.
  bool pending() const { return m_state->pending.load(); }

private:
  struct State
  {
    explicit State(QQuickItem* target) : item(target) {}
    // Read and written on the GUI thread only (set while it is blocked).
    QPointer<QQuickItem> item;
    std::atomic<bool> pending {false};
  };

  std::shared_ptr<State> m_state;
};

}
