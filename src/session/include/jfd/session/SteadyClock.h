#pragma once

#include <jfd/core/Timers.h>

#include <chrono>

namespace jfd::session {

  // The monotonic clock of the app. The session, its timers and the media
  // backend's samples must all use one instance's time base.
  class SteadyClock final : public core::Clock {
  public:
    core::LocalInstant now() const override {
      return core::LocalInstant::fromSinceOrigin(
          std::chrono::duration_cast<core::WallTicks>(std::chrono::steady_clock::now().time_since_epoch()));
    }
  };

} // namespace jfd::session
