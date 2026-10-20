#include <jfd/session/SessionHub.h>

#include <utility>
#include <vector>

namespace jfd::session {

  SessionHub::Subscription::Subscription(Subscription&& other) noexcept
      : m_hub(std::exchange(other.m_hub, nullptr)), m_id(other.m_id) {}

  SessionHub::Subscription& SessionHub::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
      reset();
      m_hub = std::exchange(other.m_hub, nullptr);
      m_id = other.m_id;
    }
    return *this;
  }

  SessionHub::Subscription::~Subscription() { reset(); }

  void SessionHub::Subscription::reset() {
    if (m_hub) m_hub->unsubscribe(m_id);
    m_hub = nullptr;
  }

  SessionHub::Subscription SessionHub::subscribe(SnapshotHandler onSnapshot,
                                                 NoticeHandler onNotice) {
    const std::uint64_t id = ++m_nextId;
    m_subscribers.emplace(id, Subscriber{std::move(onSnapshot), std::move(onNotice)});
    Subscription subscription(this, id);
    // Copied: the handler may unsubscribe itself while it runs.
    if (const SnapshotHandler handler = m_subscribers.at(id).onSnapshot) handler(m_latest);
    return subscription;
  }

  // Calls each subscriber present when the call began and still subscribed
  // when its turn comes.
  template <class Call> void SessionHub::each(Call call) {
    std::vector<std::uint64_t> ids;
    ids.reserve(m_subscribers.size());
    for (const auto& entry : m_subscribers) ids.push_back(entry.first);
    for (const std::uint64_t id : ids) {
      const auto it = m_subscribers.find(id);
      if (it == m_subscribers.end()) continue;
      const Subscriber subscriber = it->second;
      call(subscriber);
    }
  }

  void SessionHub::snapshotChanged(const core::SessionSnapshot& snapshot) {
    m_latest = snapshot;
    // A handler may cause the session to publish again; m_latest is copied so
    // every subscriber in this round sees the same snapshot.
    const core::SessionSnapshot current = m_latest;
    each([&](const Subscriber& s) {
      if (s.onSnapshot) s.onSnapshot(current);
    });
  }

  void SessionHub::notify(const core::SessionNotice& notice) {
    each([&](const Subscriber& s) {
      if (s.onNotice) s.onNotice(notice);
    });
  }

} // namespace jfd::session
