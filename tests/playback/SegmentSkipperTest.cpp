#include "../support/DoctestQt.h"

#include <jfd/playback/SegmentSkipper.h>

#include <vector>

using namespace jfd;
using namespace jfd::playback;
using core::PlaybackPhase;

namespace {

  constexpr std::int64_t Second = 10'000'000;

  api::MediaSegment segment(const char* type, std::int64_t start, std::int64_t end) {
    api::MediaSegment s;
    s.type = QString::fromLatin1(type);
    s.startTicks = start * Second;
    s.endTicks = end * Second;
    return s;
  }

  struct Fixture {
    std::vector<core::SessionInput> posted;
    SegmentSkipper skipper{[this](core::SessionInput input) { posted.push_back(std::move(input)); }};
    core::EpochCounter<core::MediaEpochTag> files;
    core::MediaEpoch epoch = files.advance();
    core::SessionSnapshot snapshot;

    Fixture() {
      skipper.begin(epoch, {segment("Outro", 1300, 1440), segment("Unknown", 0, 5),
                            segment("Intro", 60, 150)});
      snapshot.media = epoch;
      snapshot.duration = core::MediaTicks::fromTicks(1440 * Second);
    }

    void at(std::int64_t seconds, PlaybackPhase phase = PlaybackPhase::Playing) {
      snapshot.phase = phase;
      snapshot.position = *core::MediaTicks::fromTicks(seconds * Second);
      skipper.snapshotChanged(snapshot);
    }

    std::int64_t seekTarget() const {
      REQUIRE_FALSE(posted.empty());
      REQUIRE(std::holds_alternative<core::intent::Seek>(posted.back()));
      return std::get<core::intent::Seek>(posted.back()).position.ticks();
    }
  };

} // namespace

TEST_CASE("The skip button shows inside a skippable segment only") {
  Fixture f;
  f.at(2);
  CHECK_FALSE(f.skipper.current());
  f.at(70);
  REQUIRE(f.skipper.current());
  CHECK(f.skipper.current()->type == QStringLiteral("Intro"));
  f.at(150);
  CHECK_FALSE(f.skipper.current());
  CHECK(f.posted.empty());
}

TEST_CASE("Skip seeks to the segment's end, short of the file's end") {
  Fixture f;
  f.skipper.skip();
  CHECK(f.posted.empty());
  f.at(70, PlaybackPhase::Paused);
  f.skipper.skip();
  CHECK(f.seekTarget() == 150 * Second);
  CHECK_FALSE(f.skipper.current());

  f.at(1310);
  f.skipper.skip();
  CHECK(f.seekTarget() < 1440 * Second);
  CHECK(f.seekTarget() > 1439 * Second);
}

TEST_CASE("Auto-skip seeks past a segment once, and only while playing") {
  Fixture f;
  f.skipper.setAutoSkip(true);
  f.at(65, PlaybackPhase::Paused);
  CHECK(f.posted.empty());
  f.at(65);
  CHECK(f.seekTarget() == 150 * Second);
  // The viewer seeks back into the intro: it plays this time.
  f.at(80);
  CHECK(f.posted.size() == 1);
}

TEST_CASE("Snapshots of another file are ignored and a new file resets") {
  Fixture f;
  f.skipper.setAutoSkip(true);
  f.snapshot.media = f.files.advance();
  f.at(70);
  CHECK(f.posted.empty());
  CHECK_FALSE(f.skipper.current());

  f.skipper.begin(f.snapshot.media, {segment("Recap", 0, 30)});
  f.at(10);
  CHECK(f.seekTarget() == 30 * Second);
}
