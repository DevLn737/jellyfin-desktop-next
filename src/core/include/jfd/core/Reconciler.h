#pragma once

#include <jfd/core/Time.h>

#include <optional>
#include <variant>

// The SyncPlay drift decision as a pure function. Defaults come from Jellyfin
// Web's PlaybackCore (speed correction 60-3000 ms over 1000 ms, skip from
// 400 ms); they are starting points to be measured, not acceptance limits.
namespace jfd::core {

  struct ReconcilerSettings {
    bool enabled = true;
    bool useSpeed = true;
    bool useSkip = true;
    MediaDelta speedMinimum = MediaDelta::fromMilliseconds(60);
    MediaDelta speedMaximum = MediaDelta::fromMilliseconds(3000);
    WallTicks speedDuration = std::chrono::milliseconds(1000);
    WallTicks correctionMaximum = std::chrono::milliseconds(5000);
    MediaDelta skipMinimum = MediaDelta::fromMilliseconds(400);
  };

  // Where the group is: at `position` at server instant `when`, moving at
  // `baseRate` (the last Unpause command).
  struct GroupTimeline {
    MediaTicks position;
    ServerInstant when;
    PlaybackRate baseRate = PlaybackRate::normal();
  };

  // Expected group position at `at` minus `actual`. Positive means we are
  // behind. The projection may lie before zero, so this stays a difference.
  MediaDelta groupDrift(const GroupTimeline& group, ServerInstant at, MediaTicks actual);

  // The group position at `at`; empty when the projection is before zero.
  std::optional<MediaTicks> groupPosition(const GroupTimeline& group, ServerInstant at);

  namespace drift {
    struct InSync {};
    struct SpeedCorrection {
      PlaybackRate rate = PlaybackRate::normal();
      WallTicks duration{0};
    };
    struct SkipTo {
      MediaTicks position;
    };
  } // namespace drift

  using ReconcileAction =
      std::variant<drift::InSync, drift::SpeedCorrection, drift::SkipTo>;

  struct ReconcileDecision {
    MediaDelta drift;
    ReconcileAction action;
  };

  // `rateAvailable` is false when the backend cannot change rate.
  ReconcileDecision reconcile(const GroupTimeline& group, ServerInstant sampledAt,
                              MediaTicks actual, const ReconcilerSettings& settings,
                              bool rateAvailable = true);

  // round(base + drift / duration), clamped to the effective range, and the
  // wall time needed at that rate. Empty when the rate does not change or
  // the correction would last longer than the maximum.
  std::optional<drift::SpeedCorrection>
  speedCorrection(PlaybackRate base, MediaDelta drift, WallTicks preferredDuration,
                  WallTicks maximumDuration);

} // namespace jfd::core
