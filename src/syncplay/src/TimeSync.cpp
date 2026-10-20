#include <jfd/syncplay/TimeSync.h>

namespace jfd::syncplay {

  std::optional<TimeSync::Measurement> TimeSync::record(LocalInstant sent, const UtcTimeReply& reply,
                                                        LocalInstant received) {
    const WallTicks serverHeld = reply.responseTransmission - reply.requestReception;
    const WallTicks roundTrip = (received - sent) - serverHeld;
    if (roundTrip.count() < 0 || serverHeld.count() < 0) return std::nullopt;
    // ((T1 - t0) + (T2 - t3)) / 2 with T server instants and t local ones.
    const WallTicks outbound = reply.requestReception.sinceOrigin() - sent.sinceOrigin();
    const WallTicks inbound = reply.responseTransmission.sinceOrigin() - received.sinceOrigin();
    Measurement measurement{ClockOffset{(outbound + inbound) / 2}, roundTrip};
    m_measurements.push_back(measurement);
    if (m_measurements.size() > TrackedMeasurements) m_measurements.pop_front();
    ++m_count;
    return best();
  }

  std::optional<TimeSync::Measurement> TimeSync::best() const {
    std::optional<Measurement> best;
    for (const auto& m : m_measurements)
      if (!best || m.roundTrip < best->roundTrip) best = m;
    return best;
  }

  WallTicks TimeSync::nextPollDelay() const {
    if (m_count < GreedyMeasurements) return std::chrono::seconds(1);
    return std::chrono::seconds(60);
  }

} // namespace jfd::syncplay
