#pragma once

#include <chrono>
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

  // A signed distance on the media timeline. Projections of a group position
  // to another instant may land before zero, so arithmetic stays in this type
  // and is converted to milliseconds once, at the end.
  class MediaDelta {
  public:
    constexpr MediaDelta() = default;
    static constexpr MediaDelta fromTicks(std::int64_t ticks) { return MediaDelta(ticks); }
    static constexpr MediaDelta fromMilliseconds(std::int64_t ms) {
      return MediaDelta(ms * TicksPerMillisecond);
    }

    constexpr std::int64_t ticks() const { return m_ticks; }
    // Rounded half away from zero, so -0.5 ms and 0.5 ms are symmetric.
    constexpr std::int64_t milliseconds() const {
      const std::int64_t half = TicksPerMillisecond / 2;
      return m_ticks >= 0 ? (m_ticks + half) / TicksPerMillisecond
                          : -((-m_ticks + half) / TicksPerMillisecond);
    }
    constexpr MediaDelta abs() const { return MediaDelta(m_ticks < 0 ? -m_ticks : m_ticks); }

    constexpr MediaDelta operator-() const { return MediaDelta(-m_ticks); }
    constexpr MediaDelta operator+(MediaDelta other) const {
      return MediaDelta(m_ticks + other.m_ticks);
    }
    constexpr MediaDelta operator-(MediaDelta other) const {
      return MediaDelta(m_ticks - other.m_ticks);
    }
    constexpr auto operator<=>(const MediaDelta&) const = default;

  private:
    constexpr explicit MediaDelta(std::int64_t ticks) : m_ticks(ticks) {}
    std::int64_t m_ticks = 0;
  };

  constexpr MediaDelta operator-(MediaTicks a, MediaTicks b) {
    return MediaDelta::fromTicks(a.ticks() - b.ticks());
  }

  // Moves a position along the timeline; fails when the result is before zero.
  constexpr std::optional<MediaTicks> advance(MediaTicks position, MediaDelta delta) {
    return MediaTicks::fromTicks(position.ticks() + delta.ticks());
  }

  // Wall-clock durations use the same 100 ns unit as the server's timestamps.
  using WallTicks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;

  // An instant on one particular clock. Local and server instants are distinct
  // types: they meet only through ClockOffset.
  template <class Tag> class Instant {
  public:
    constexpr Instant() = default;
    static constexpr Instant fromSinceOrigin(WallTicks sinceOrigin) { return Instant(sinceOrigin); }

    constexpr WallTicks sinceOrigin() const { return m_sinceOrigin; }

    constexpr Instant operator+(WallTicks d) const { return Instant(m_sinceOrigin + d); }
    constexpr Instant operator-(WallTicks d) const { return Instant(m_sinceOrigin - d); }
    constexpr WallTicks operator-(Instant other) const {
      return m_sinceOrigin - other.m_sinceOrigin;
    }
    constexpr auto operator<=>(const Instant&) const = default;

  private:
    constexpr explicit Instant(WallTicks sinceOrigin) : m_sinceOrigin(sinceOrigin) {}
    WallTicks m_sinceOrigin{0};
  };

  struct LocalClockTag;
  struct ServerClockTag;

  // Local monotonic time. Never jumps with the system clock.
  using LocalInstant = Instant<LocalClockTag>;
  // Server UTC time (When, EmittedAt, LastUpdate), as 100 ns ticks since the Unix epoch.
  using ServerInstant = Instant<ServerClockTag>;

  // The current estimate of server time minus local monotonic time, produced
  // by TimeSync in the syncplay module.
  struct ClockOffset {
    WallTicks serverMinusLocal{0};

    constexpr ServerInstant toServer(LocalInstant local) const {
      return ServerInstant::fromSinceOrigin(local.sinceOrigin() + serverMinusLocal);
    }
    constexpr LocalInstant toLocal(ServerInstant server) const {
      return LocalInstant::fromSinceOrigin(server.sinceOrigin() - serverMinusLocal);
    }
    constexpr auto operator<=>(const ClockOffset&) const = default;
  };

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

    static constexpr std::optional<PlaybackRate> basePerMille(std::int32_t perMille) {
      if (perMille < MinimumBase || perMille > Maximum) return std::nullopt;
      return PlaybackRate(perMille);
    }

    static constexpr std::optional<PlaybackRate> effectivePerMille(std::int32_t perMille) {
      if (perMille < MinimumEffective || perMille > Maximum) return std::nullopt;
      return PlaybackRate(perMille);
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

  // Media time that passes during a wall-clock interval at a rate:
  // round(elapsedWallTicks * rate), as the server computes it.
  // Split so any instant difference (up to centuries) times 5x cannot overflow.
  constexpr MediaDelta mediaElapsed(WallTicks wall, PlaybackRate rate) {
    const bool negative = wall.count() < 0;
    const std::int64_t magnitude = negative ? -wall.count() : wall.count();
    const std::int64_t whole = (magnitude / 1000) * rate.perMille();
    const std::int64_t part = ((magnitude % 1000) * rate.perMille() + 500) / 1000;
    return MediaDelta::fromTicks(negative ? -(whole + part) : whole + part);
  }

  // Wall-clock time needed to cover a media distance at a rate:
  // round(mediaTicks / rate).
  constexpr WallTicks wallElapsed(MediaDelta media, PlaybackRate rate) {
    const std::int64_t scaled = media.ticks() * 1000;
    const std::int64_t per = rate.perMille();
    const std::int64_t rounded =
        scaled >= 0 ? (scaled + per / 2) / per : -((-scaled + per / 2) / per);
    return WallTicks(rounded);
  }

} // namespace jfd::core
