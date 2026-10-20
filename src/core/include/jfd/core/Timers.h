#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/Time.h>

#include <compare>
#include <cstdint>

// Time sources for the playback session. The app implements them with a
// monotonic clock and QTimer; the simulator implements them with fake time.
namespace jfd::core {

  enum class TimerPurpose : std::uint8_t {
    // A SyncPlay command scheduled for its When instant.
    GroupCommand,
    // Re-enables drift correction after a command or a correction.
    CorrectionCooldown,
    // Ends a temporary rate correction and restores the group base rate.
    CorrectionEnd,
    // Gives up waiting for backend readiness after a load or seek.
    ReadyDeadline,
  };

  // Serials are never reused. A firing whose id is no longer armed in the
  // session, or whose group epoch is stale, is dropped on entry.
  struct TimerId {
    std::uint64_t serial = 0;
    TimerPurpose purpose = TimerPurpose::GroupCommand;
    GroupEpoch group;
    constexpr auto operator<=>(const TimerId&) const = default;
  };

  class Clock {
  public:
    virtual ~Clock() = default;
    virtual LocalInstant now() const = 0;
  };

  // Delivers TimerFired{id} to the session's queue at or after `due`.
  // Cancelling a timer that already fired, or was never armed, does nothing.
  class Timers {
  public:
    virtual ~Timers() = default;
    virtual void arm(TimerId id, LocalInstant due) = 0;
    virtual void cancel(TimerId id) = 0;
  };

} // namespace jfd::core
