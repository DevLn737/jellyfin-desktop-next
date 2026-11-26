#include <doctest/doctest.h>
#include <jfd/core/Reconciler.h>

using namespace jfd::core;
using namespace std::chrono_literals;

namespace {
  MediaTicks ms(std::int64_t value) { return *MediaTicks::fromMilliseconds(value); }
  ServerInstant at(std::chrono::milliseconds t) { return ServerInstant::fromSinceOrigin(t); }
} // namespace

TEST_CASE("Drift projects the group position with the base rate") {
  const GroupTimeline group{ms(10'000), at(100'000ms), *PlaybackRate::base(1.5)};
  // 2 s later the group is at 13 s.
  CHECK(groupDrift(group, at(102'000ms), ms(13'000)).milliseconds() == 0);
  CHECK(groupDrift(group, at(102'000ms), ms(12'800)).milliseconds() == 200);
  CHECK(groupDrift(group, at(102'000ms), ms(13'300)).milliseconds() == -300);
}

TEST_CASE("Drift may be measured against a projection before zero") {
  const GroupTimeline group{ms(100), at(100'000ms), PlaybackRate::normal()};
  // Sampled 1 s before When: the projection is -900 ms.
  CHECK(groupDrift(group, at(99'000ms), ms(0)).milliseconds() == -900);
  CHECK_FALSE(groupPosition(group, at(99'000ms)));
}

TEST_CASE("Small drift is in sync") {
  const GroupTimeline group{ms(0), at(0ms), PlaybackRate::normal()};
  const auto decision = reconcile(group, at(10'000ms), ms(9'950), {});
  CHECK(decision.drift.milliseconds() == 50);
  CHECK(std::holds_alternative<drift::InSync>(decision.action));
}

TEST_CASE("Medium drift corrects speed relative to the base rate") {
  const GroupTimeline group{ms(0), at(0ms), *PlaybackRate::base(1.25)};
  // Group at 12.5 s; we are 300 ms behind.
  const auto decision = reconcile(group, at(10'000ms), ms(12'200), {});
  const auto* speed = std::get_if<drift::SpeedCorrection>(&decision.action);
  REQUIRE(speed);
  // Within 10 % of the group's 1.25x, for as long as 300 ms need at that.
  CHECK(speed->rate.perMille() == 1375);
  CHECK(speed->duration == WallTicks(2400ms));

  // Ahead by 300 ms: slow down.
  const auto ahead = reconcile(group, at(10'000ms), ms(12'800), {});
  const auto* slow = std::get_if<drift::SpeedCorrection>(&ahead.action);
  REQUIRE(slow);
  CHECK(slow->rate.perMille() == 1125);
  CHECK(slow->duration == WallTicks(2400ms));

  // 80 ms over 1 s fit in the band: the plain formula applies.
  const auto small = reconcile(group, at(10'000ms), ms(12'420), {});
  const auto* gentle = std::get_if<drift::SpeedCorrection>(&small.action);
  REQUIRE(gentle);
  CHECK(gentle->rate.perMille() == 1330);
  CHECK(gentle->duration == WallTicks(1000ms));
}

TEST_CASE("Speed stays within 10 % of the group rate; drift it cannot make up in 5 s is skipped") {
  const GroupTimeline group{ms(0), at(0ms), PlaybackRate::normal()};
  // 500 ms behind: 1.1x for 5 s, the longest correction.
  const auto edge = reconcile(group, at(10'000ms), ms(9'500), {});
  const auto* speed = std::get_if<drift::SpeedCorrection>(&edge.action);
  REQUIRE(speed);
  CHECK(speed->rate.perMille() == 1100);
  CHECK(speed->duration == WallTicks(5000ms));
  // 2.5 s ahead: Web's formula asks 0.2x; capped at 0.9x it would take
  // 25 s, so it is a skip.
  const auto ahead = reconcile(group, at(10'000ms), ms(12'500), {});
  const auto* skip = std::get_if<drift::SkipTo>(&ahead.action);
  REQUIRE(skip);
  CHECK(skip->position.milliseconds() == 10'000);
  // 800 ms behind, under the speed maximum: skipped too.
  CHECK(std::holds_alternative<drift::SkipTo>(reconcile(group, at(10'000ms), ms(9'200), {}).action));
  // The band follows the group rate: 0.45x-0.55x at 0.5x.
  const GroupTimeline slow{ms(0), at(0ms), *PlaybackRate::base(0.5)};
  const auto halfDecision = reconcile(slow, at(10'000ms), ms(4'900), {});
  const auto* half = std::get_if<drift::SpeedCorrection>(&halfDecision.action);
  REQUIRE(half);
  CHECK(half->rate.perMille() == 550);
  CHECK(half->duration == WallTicks(2000ms));
  // Without a band, Web's range.
  ReconcilerSettings web;
  web.speedBandPerMille = 0;
  const auto fastDecision = reconcile(group, at(10'000ms), ms(7'500), web);
  const auto* fast = std::get_if<drift::SpeedCorrection>(&fastDecision.action);
  REQUIRE(fast);
  CHECK(fast->rate.perMille() == 3500);
}

TEST_CASE("Large drift skips to the group position") {
  const GroupTimeline group{ms(0), at(0ms), PlaybackRate::normal()};
  const auto decision = reconcile(group, at(10'000ms), ms(5'000), {});
  const auto* skip = std::get_if<drift::SkipTo>(&decision.action);
  REQUIRE(skip);
  CHECK(skip->position.milliseconds() == 10'000);
}

TEST_CASE("A correction that cannot be expressed falls back to a skip") {
  // At 0.25x a 2 s lead needs a rate below 0.2x for longer than allowed.
  const GroupTimeline group{ms(0), at(0ms), *PlaybackRate::base(0.25)};
  const auto decision = reconcile(group, at(40'000ms), ms(12'000), {});
  CHECK(decision.drift.milliseconds() == -2'000);
  CHECK(std::holds_alternative<drift::SkipTo>(decision.action));
}

TEST_CASE("Disabled correction only measures") {
  ReconcilerSettings settings;
  settings.enabled = false;
  const GroupTimeline group{ms(0), at(0ms), PlaybackRate::normal()};
  const auto decision = reconcile(group, at(10'000ms), ms(5'000), settings);
  CHECK(decision.drift.milliseconds() == 5'000);
  CHECK(std::holds_alternative<drift::InSync>(decision.action));
}

TEST_CASE("speedCorrection matches Jellyfin Web's PlaybackRate.ts") {
  // base 1, diff 500 ms, 1000 ms -> rate 1.5 for 1000 ms.
  auto c = speedCorrection(PlaybackRate::normal(), MediaDelta::fromMilliseconds(500), 1000ms, 5000ms);
  REQUIRE(c);
  CHECK(c->rate.perMille() == 1500);
  CHECK(c->duration == WallTicks(1000ms));
  // Clamped at 5x: diff 6000 ms needs 6000/4 = 1500 ms.
  c = speedCorrection(PlaybackRate::normal(), MediaDelta::fromMilliseconds(6000), 1000ms, 5000ms);
  REQUIRE(c);
  CHECK(c->rate.perMille() == 5000);
  CHECK(c->duration == WallTicks(1500ms));
  CHECK_FALSE(speedCorrection(PlaybackRate::normal(), MediaDelta{}, 1000ms, 5000ms));
}
