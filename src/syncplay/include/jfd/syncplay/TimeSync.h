#pragma once

#include <jfd/core/Time.h>
#include <jfd/syncplay/Protocol.h>

#include <deque>
#include <optional>

// Estimates server time from GET /GetUtcTime exchanges, as Jellyfin Web's
// TimeSync does: NTP-style offset per exchange, the exchange with the lowest
// round trip of the last eight wins, polled every second for the first three
// exchanges and every minute after that.
namespace jfd::syncplay {

  class TimeSync {
  public:
    static constexpr std::size_t TrackedMeasurements = 8;
    static constexpr std::size_t GreedyMeasurements = 3;

    struct Measurement {
      ClockOffset offset;
      WallTicks roundTrip{0};
    };

    // Records one exchange. Returns the measurement now in use, or empty when
    // the exchange is impossible (negative round trip).
    std::optional<Measurement> record(LocalInstant sent, const UtcTimeReply& reply,
                                      LocalInstant received);

    std::optional<Measurement> best() const;
    WallTicks nextPollDelay() const;
    void reset() { m_measurements.clear(); m_count = 0; }

  private:
    std::deque<Measurement> m_measurements;
    std::size_t m_count = 0;
  };

} // namespace jfd::syncplay
