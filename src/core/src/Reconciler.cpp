#include <jfd/core/Reconciler.h>

#include <algorithm>

namespace jfd::core {

  MediaDelta groupDrift(const GroupTimeline& group, ServerInstant at, MediaTicks actual) {
    return (group.position - actual) + mediaElapsed(at - group.when, group.baseRate);
  }

  std::optional<MediaTicks> groupPosition(const GroupTimeline& group, ServerInstant at) {
    return advance(group.position, mediaElapsed(at - group.when, group.baseRate));
  }

  std::optional<drift::SpeedCorrection>
  speedCorrection(PlaybackRate base, MediaDelta drift, WallTicks preferredDuration,
                  WallTicks maximumDuration) {
    if (preferredDuration.count() <= 0 || maximumDuration.count() <= 0) return std::nullopt;
    // drift ticks per wall tick, in per mille: drift / duration * 1000.
    const std::int64_t requested =
        base.perMille() + (drift.ticks() * 1000) / preferredDuration.count();
    const auto clamped = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(requested, PlaybackRate::MinimumEffective, PlaybackRate::Maximum));
    const std::int32_t delta = clamped - base.perMille();
    if (delta == 0) return std::nullopt;
    // Wall time to cover the drift at the rate difference: drift / delta.
    const std::int64_t wall = (drift.ticks() * 1000) / delta;
    if (wall <= 0 || WallTicks(wall) > maximumDuration) return std::nullopt;
    const auto rate = PlaybackRate::effectivePerMille(clamped);
    if (!rate) return std::nullopt;
    return drift::SpeedCorrection{*rate, WallTicks(wall)};
  }

  ReconcileDecision reconcile(const GroupTimeline& group, ServerInstant sampledAt,
                              MediaTicks actual, const ReconcilerSettings& settings,
                              bool rateAvailable) {
    const MediaDelta drift = groupDrift(group, sampledAt, actual);
    ReconcileDecision decision{drift, drift::InSync{}};
    if (!settings.enabled) return decision;

    const MediaDelta size = drift.abs();
    const auto target = groupPosition(group, sampledAt);
    const auto skip = [&]() -> ReconcileAction {
      if (!settings.useSkip || !target) return drift::InSync{};
      return drift::SkipTo{*target};
    };

    if (rateAvailable && settings.useSpeed && size >= settings.speedMinimum &&
        size < settings.speedMaximum) {
      if (const auto correction = speedCorrection(group.baseRate, drift, settings.speedDuration,
                                                  settings.correctionMaximum)) {
        decision.action = *correction;
      } else {
        // As in Jellyfin Web: a correction that cannot be expressed falls back
        // to skipping, whatever the skip minimum.
        decision.action = skip();
      }
    } else if (size >= settings.skipMinimum) {
      decision.action = skip();
    }
    return decision;
  }

} // namespace jfd::core
