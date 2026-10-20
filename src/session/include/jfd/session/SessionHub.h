#pragma once

#include <jfd/core/SessionSnapshot.h>

#include <cstdint>
#include <functional>
#include <map>

// The playback session has one observer; this is it. The hub hands every
// snapshot and notice on to each subscriber: the UI, the media integrations
// (SMTC, MPRIS, Discord) and diagnostics.
namespace jfd::session {

  class SessionHub final : public core::SessionObserver {
  public:
    using SnapshotHandler = std::function<void(const core::SessionSnapshot&)>;
    using NoticeHandler = std::function<void(const core::SessionNotice&)>;

    // Ends the subscription when destroyed. Must not outlive the hub.
    class Subscription {
    public:
      Subscription() = default;
      Subscription(Subscription&& other) noexcept;
      Subscription& operator=(Subscription&& other) noexcept;
      Subscription(const Subscription&) = delete;
      Subscription& operator=(const Subscription&) = delete;
      ~Subscription();

      void reset();

    private:
      friend class SessionHub;
      Subscription(SessionHub* hub, std::uint64_t id) : m_hub(hub), m_id(id) {}
      SessionHub* m_hub = nullptr;
      std::uint64_t m_id = 0;
    };

    // The handler is called at once with the latest snapshot, then after
    // every change. Either handler may be empty. Handlers may subscribe and
    // unsubscribe, also from inside a call; a subscriber added during a call
    // is not called again in that round.
    [[nodiscard]] Subscription subscribe(SnapshotHandler onSnapshot, NoticeHandler onNotice = {});

    const core::SessionSnapshot& latest() const { return m_latest; }

    // core::SessionObserver
    void snapshotChanged(const core::SessionSnapshot& snapshot) override;
    void notify(const core::SessionNotice& notice) override;

  private:
    struct Subscriber {
      SnapshotHandler onSnapshot;
      NoticeHandler onNotice;
    };

    void unsubscribe(std::uint64_t id) { m_subscribers.erase(id); }
    template <class Call> void each(Call call);

    std::map<std::uint64_t, Subscriber> m_subscribers;
    std::uint64_t m_nextId = 0;
    core::SessionSnapshot m_latest;
  };

} // namespace jfd::session
