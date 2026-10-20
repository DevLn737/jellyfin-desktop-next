#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>

#include <atomic>

namespace jfd::media {

// mpv's update callback may come from any thread; it only queues a coalesced
// repaint request for the item on the GUI thread.
class RepaintRequest
{
public:
  explicit RepaintRequest(QQuickItem* item) : m_item(item) {}
  void request()
  {
    if (m_pending.exchange(true))
      return;
    QMetaObject::invokeMethod(m_item, [this, item = m_item] {
      m_pending.store(false);
      if (item)
        item->update();
    }, Qt::QueuedConnection);
  }

private:
  QPointer<QQuickItem> m_item;
  std::atomic<bool> m_pending {false};
};

}
