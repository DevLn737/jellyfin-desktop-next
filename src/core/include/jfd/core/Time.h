#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>

// Time and rate values shared by the playback session, the SyncPlay protocol
// and the media backend. Conversions are explicit and fail instead of clamping:
// a silently clamped position or rate is a desynchronization nobody notices.
namespace jfd::core {

  inline constexpr std::int64_t TicksPerMillisecond = 10'000;

  // A position on Jellyfin's original media timeline (100 ns ticks). Backend
  // positions are converted to this through the per-file timeline mapping.
  class MediaTicks {
  public:
    constexpr MediaTicks() = default;

    static constexpr std::optional<MediaTicks> fromTicks(std::int64_t ticks) {
      if (ticks < 0) return std::nullopt;
      return MediaTicks(ticks);
    }

    static constexpr std::optional<MediaTicks> fromMilliseconds(std::int64_t ms) {
      if (ms < 0 || ms > std::numeric_limits<std::int64_t>::max() / TicksPerMillisecond)
        return std::nullopt;
      return MediaTicks(ms * TicksPerMillisecond);
    }

    // libmpv reports seconds as doubles; round to the nearest millisecond as
    // the existing client does, so positions compare equal across clients.
    static std::optional<MediaTicks> fromSeconds(double seconds) {
      if (!std::isfinite(seconds) || seconds < 0.0) return std::nullopt;
      const double ms = std::round(seconds * 1000.0);
      if (!(ms < static_cast<double>(std::numeric_limits<std::int64_t>::max() /
                                     TicksPerMillisecond)))
        return std::nullopt;
      return fromMilliseconds(static_cast<std::int64_t>(ms));
    }

    constexpr std::int64_t ticks() const { return m_ticks; }
    constexpr std::int64_t milliseconds() const { return m_ticks / TicksPerMillisecond; }
    constexpr double seconds() const { return static_cast<double>(m_ticks) / 1e7; }

    constexpr auto operator<=>(const MediaTicks&) const = default;

  private:
    constexpr explicit MediaTicks(std::int64_t ticks) : m_ticks(ticks) {}
    std::int64_t m_ticks = 0;
  };

  // Signed difference between two timeline positions, in milliseconds.
  constexpr std::int64_t differenceMilliseconds(MediaTicks a, MediaTicks b) {
    return (a.ticks() - b.ticks()) / TicksPerMillisecond;
  }

  // Playback rate in per-mille (1000 = 1x), the unit used at the backend
  // boundary. Group base rates and temporary SyncPlay corrections have
  // different valid ranges; a correction may go below the minimum base rate.
  class PlaybackRate {
  public:
    static constexpr std::int32_t MinimumBase = 250;
    static constexpr std::int32_t MinimumEffective = 200;
    static constexpr std::int32_t Maximum = 5000;

    static constexpr PlaybackRate normal() { return PlaybackRate(1000); }

    // A group base rate as sent by the server (PlaybackRate, 0.25..5).
    static std::optional<PlaybackRate> base(double rate) {
      return fromDouble(rate, MinimumBase);
    }

    // An effective rate including a temporary correction (0.2..5).
    static std::optional<PlaybackRate> effective(double rate) {
      return fromDouble(rate, MinimumEffective);
    }

    constexpr std::int32_t perMille() const { return m_perMille; }
    constexpr double value() const { return m_perMille / 1000.0; }

    constexpr auto operator<=>(const PlaybackRate&) const = default;

  private:
    constexpr explicit PlaybackRate(std::int32_t perMille) : m_perMille(perMille) {}

    static std::optional<PlaybackRate> fromDouble(double rate, std::int32_t minimum) {
      if (!std::isfinite(rate)) return std::nullopt;
      const double perMille = std::round(rate * 1000.0);
      if (perMille < minimum || perMille > Maximum) return std::nullopt;
      return PlaybackRate(static_cast<std::int32_t>(perMille));
    }

    std::int32_t m_perMille = 1000;
  };

} // namespace jfd::core
