#include "../support/DoctestQt.h"
#include "../support/SessionHarness.h"

#include <jfd/playback/PlaybackReporter.h>

#include <QCoreApplication>
#include <QElapsedTimer>

#include <vector>

using namespace jfd;
using namespace jfd::playback;
using core::PlaybackPhase;

namespace {

  struct Report {
    QString kind;
    api::PlaybackProgress progress;
    bool failed = false;
  };

  class RecordingSink final : public ReportSink {
  public:
    void started(const api::PlaybackProgress& p) override { reports.push_back({QStringLiteral("start"), p}); }
    void progress(const api::PlaybackProgress& p) override { reports.push_back({QStringLiteral("progress"), p}); }
    void stopped(const api::PlaybackProgress& p, bool failed, std::function<void()> answered) override {
      reports.push_back({QStringLiteral("stopped"), p, failed});
      if (answered) pendingAnswers.push_back(std::move(answered));
    }
    std::vector<Report> reports;
    // The server's answers to Stopped, not given yet.
    std::vector<std::function<void()>> pendingAnswers;

    QStringList events() const {
      QStringList list;
      for (const Report& r : reports)
        list.append(r.kind == QStringLiteral("progress") ? r.progress.eventName : r.kind);
      return list;
    }
  };

  constexpr std::int64_t Second = 10'000'000;

  struct Fixture {
    RecordingSink sink;
    PlaybackReporter reporter{sink};
    core::EpochCounter<core::MediaEpochTag> files;
    core::MediaEpoch epoch = files.advance();
    core::SessionSnapshot snapshot;

    Fixture() {
      reporter.setProgressInterval(std::chrono::hours(1));
      api::PlaybackStream stream;
      stream.itemId = QStringLiteral("item");
      stream.mediaSourceId = QStringLiteral("source");
      stream.playSessionId = QStringLiteral("play");
      stream.method = api::PlayMethod::Transcode;
      reporter.begin(epoch, stream, 3, std::nullopt);
      snapshot.media = epoch;
    }

    void push(PlaybackPhase phase, std::int64_t positionTicks) {
      snapshot.phase = phase;
      snapshot.position = *core::MediaTicks::fromTicks(positionTicks);
      reporter.snapshotChanged(snapshot);
    }
  };

  void spin(int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

  // Spins until done() or the deadline; coarse timers (Windows) fire late.
  template <typename Done>
  void spinUntil(Done done, int milliseconds = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < milliseconds) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }

} // namespace

TEST_CASE("Start is sent once the file plays, with the stream's identity") {
  Fixture f;
  f.push(PlaybackPhase::Loading, 0);
  f.push(PlaybackPhase::Buffering, 0);
  CHECK(f.sink.reports.empty());
  f.push(PlaybackPhase::Playing, 5 * Second);
  REQUIRE(f.sink.events() == QStringList{QStringLiteral("start")});
  const api::PlaybackProgress& start = f.sink.reports.front().progress;
  CHECK(start.itemId == QStringLiteral("item"));
  CHECK(start.playSessionId == QStringLiteral("play"));
  CHECK(start.playMethod == api::PlayMethod::Transcode);
  CHECK(start.positionTicks == 5 * Second);
  CHECK(start.audioStreamIndex == 3);
  CHECK_FALSE(start.subtitleStreamIndex);
  CHECK_FALSE(start.isPaused);
}

TEST_CASE("Pause, resume, seek, rate, track and volume changes are reported at once") {
  Fixture f;
  f.push(PlaybackPhase::Playing, 0);
  f.push(PlaybackPhase::Paused, 1 * Second);
  f.push(PlaybackPhase::Buffering, 1 * Second);
  f.push(PlaybackPhase::Playing, 1 * Second);
  f.push(PlaybackPhase::Playing, 600 * Second);
  f.push(PlaybackPhase::Playing, 600 * Second);

  f.snapshot.personalRate = *core::PlaybackRate::base(1.5);
  f.snapshot.effectiveRate = *core::PlaybackRate::base(1.5);
  f.push(PlaybackPhase::Playing, 600 * Second);
  // A SyncPlay correction changes only the effective rate: not reported.
  f.snapshot.effectiveRate = *core::PlaybackRate::effective(1.6);
  f.push(PlaybackPhase::Playing, 600 * Second);

  f.reporter.setTracks(4, 7);
  f.reporter.setVolume(40, false);
  f.reporter.setVolume(40, false);

  CHECK(f.sink.events() == QStringList{QStringLiteral("start"), QStringLiteral("Pause"), QStringLiteral("Unpause"), QStringLiteral("TimeUpdate"),
                                       QStringLiteral("PlaybackRateChange"), QStringLiteral("AudioTrackChange"),
                                       QStringLiteral("SubtitleTrackChange"), QStringLiteral("VolumeChange")});
  const api::PlaybackProgress& last = f.sink.reports.back().progress;
  CHECK(last.playbackRate == doctest::Approx(1.5));
  CHECK(last.audioStreamIndex == 4);
  CHECK(last.subtitleStreamIndex == 7);
  CHECK(last.volumeLevel == 40);
}

TEST_CASE("A seek first seen while loading is reported once playback is active again") {
  Fixture f;
  f.push(PlaybackPhase::Playing, 0);
  f.push(PlaybackPhase::Paused, 1 * Second);
  f.push(PlaybackPhase::Loading, 200 * Second);
  f.push(PlaybackPhase::Paused, 200 * Second);
  REQUIRE(f.sink.events() == QStringList{QStringLiteral("start"), QStringLiteral("Pause"), QStringLiteral("TimeUpdate")});
  CHECK(f.sink.reports.back().progress.positionTicks == 200 * Second);
  CHECK(f.sink.reports.back().progress.isPaused);
  f.push(PlaybackPhase::Paused, 200 * Second);
  CHECK(f.sink.events().size() == 3);
}

TEST_CASE("Progress is sent periodically while playing, not while paused") {
  Fixture f;
  f.reporter.setProgressInterval(std::chrono::milliseconds(40));
  f.push(PlaybackPhase::Playing, 0);
  spinUntil([&] { return f.sink.reports.size() >= 3; });
  const auto playing = f.sink.reports.size();
  CHECK(playing >= 3);
  f.push(PlaybackPhase::Paused, 1 * Second);
  const auto paused = f.sink.reports.size();
  spin(150);
  CHECK(f.sink.reports.size() == paused);
}

TEST_CASE("Stopped carries the final position and is sent once") {
  Fixture f;
  f.push(PlaybackPhase::Playing, 0);
  f.push(PlaybackPhase::Ended, 1300 * Second);
  f.push(PlaybackPhase::Playing, 1400 * Second);
  f.reporter.end();
  REQUIRE(f.sink.events() == QStringList{QStringLiteral("start"), QStringLiteral("stopped")});
  CHECK(f.sink.reports.back().progress.positionTicks == 1300 * Second);
  CHECK_FALSE(f.sink.reports.back().failed);
}

TEST_CASE("Stopping, which leaves the session without a file, is reported") {
  Fixture f;
  f.push(PlaybackPhase::Playing, 0);
  core::SessionSnapshot idle;
  idle.phase = PlaybackPhase::Idle;
  f.reporter.snapshotChanged(idle);
  REQUIRE(f.sink.events() == QStringList{QStringLiteral("start"), QStringLiteral("stopped")});
  CHECK_FALSE(f.sink.reports.back().failed);
  f.reporter.end();
  CHECK(f.sink.reports.size() == 2);
}

TEST_CASE("A failure is reported as failed, and a new file ends the old report") {
  Fixture f;
  f.push(PlaybackPhase::Playing, 0);
  f.push(PlaybackPhase::Failed, 2 * Second);
  CHECK(f.sink.reports.back().failed);

  const core::MediaEpoch old = f.epoch;
  const core::MediaEpoch next = f.files.advance();
  api::PlaybackStream stream;
  stream.itemId = QStringLiteral("next");
  f.reporter.begin(next, stream, std::nullopt, std::nullopt);
  f.snapshot.media = next;
  f.push(PlaybackPhase::Playing, 0);
  f.reporter.begin(f.files.advance(), stream, std::nullopt, std::nullopt);
  // Snapshots of an earlier file are ignored.
  f.snapshot.media = old;
  f.push(PlaybackPhase::Playing, 9 * Second);
  CHECK(f.sink.events() == QStringList{QStringLiteral("start"), QStringLiteral("stopped"), QStringLiteral("start"), QStringLiteral("stopped")});
  CHECK(f.sink.reports.back().progress.itemId == QStringLiteral("next"));
  CHECK_FALSE(f.sink.reports.back().failed);
}

TEST_CASE("A file that never played sends nothing") {
  Fixture f;
  f.push(PlaybackPhase::Loading, 0);
  f.push(PlaybackPhase::Failed, 0);
  f.reporter.end();
  CHECK(f.sink.reports.empty());
}

namespace {

  // Feeds the reporter what a real PlaybackSession publishes, not hand-made
  // snapshots: an ended or failed file leaves the session without an epoch.
  struct SessionFixture {
    RecordingSink sink;
    PlaybackReporter reporter{sink};
    jfd::testing::SessionHarness harness;
    std::size_t delivered = 0;
    core::MediaEpoch epoch;

    SessionFixture() {
      reporter.setProgressInterval(std::chrono::hours(1));
      harness.post(core::intent::Open{harness.media()});
      epoch = harness.currentEpoch();
      api::PlaybackStream stream;
      stream.itemId = QStringLiteral("item");
      reporter.begin(epoch, stream, std::nullopt, std::nullopt);
      post(core::backend::SourceOpened{epoch, 0.0, 600.0});
      post(core::backend::PlaybackRestarted{epoch, harness.sample(0.0, false)});
    }

    // A jump in the fake session's position also reads as a seek
    // (TimeUpdate); what matters is one Start first and one Stopped last.
    bool startedAndStoppedOnce() const {
      const QStringList events = sink.events();
      return !events.isEmpty() && events.front() == QStringLiteral("start") &&
             events.back() == QStringLiteral("stopped") &&
             events.count(QStringLiteral("start")) == 1 && events.count(QStringLiteral("stopped")) == 1;
    }

    void post(core::SessionInput input) {
      harness.post(std::move(input));
      const auto& snapshots = harness.observer.snapshots;
      for (; delivered < snapshots.size(); ++delivered) reporter.snapshotChanged(snapshots[delivered]);
    }
  };

} // namespace

TEST_CASE("A file that fails in the session is reported as failed at its last position") {
  SessionFixture f;
  f.post(core::backend::Progress{f.epoch, f.harness.sample(42.0, false)});
  f.post(core::backend::Ended{f.epoch, core::backend::EndReason::Error, "loading failed"});
  REQUIRE(f.harness.snapshot().phase == PlaybackPhase::Failed);
  CHECK_FALSE(f.harness.snapshot().media.isValid());
  REQUIRE(f.startedAndStoppedOnce());
  CHECK(f.sink.reports.back().failed);
  CHECK(f.sink.reports.back().progress.positionTicks == 42 * Second);
  f.reporter.end();
  CHECK(f.sink.events().count(QStringLiteral("stopped")) == 1);
}

TEST_CASE("A file that ends or is stopped in the session is not reported as failed") {
  SUBCASE("end of file") {
    SessionFixture f;
    f.post(core::backend::Progress{f.epoch, f.harness.sample(590.0, false)});
    f.post(core::backend::Ended{f.epoch, core::backend::EndReason::EndOfFile, {}});
    REQUIRE(f.startedAndStoppedOnce());
    CHECK_FALSE(f.sink.reports.back().failed);
    CHECK(f.sink.reports.back().progress.positionTicks == 590 * Second);
  }
  SUBCASE("stop") {
    SessionFixture f;
    f.post(core::backend::Progress{f.epoch, f.harness.sample(10.0, false)});
    f.post(core::intent::Stop{});
    REQUIRE(f.startedAndStoppedOnce());
    CHECK_FALSE(f.sink.reports.back().failed);
  }
}

TEST_CASE("Quitting during playback sends Stopped at the exit position and waits for the answer") {
  SessionFixture f;
  f.post(core::backend::Progress{f.epoch, f.harness.sample(1234.0, false)});
  bool done = false;
  f.reporter.end([&] { done = true; });
  REQUIRE(f.startedAndStoppedOnce());
  CHECK(f.sink.reports.back().progress.positionTicks == 1234 * Second);
  CHECK_FALSE(f.sink.reports.back().failed);
  CHECK_FALSE(done);
  REQUIRE(f.sink.pendingAnswers.size() == 1);
  f.sink.pendingAnswers.front()();
  CHECK(done);

  // Destroying the player afterwards sends nothing more.
  bool again = false;
  f.reporter.end([&] { again = true; });
  CHECK(again);
  CHECK(f.sink.events().count(QStringLiteral("stopped")) == 1);
}

TEST_CASE("Quitting with nothing playing does not wait") {
  RecordingSink sink;
  PlaybackReporter reporter{sink};
  bool done = false;
  reporter.end([&] { done = true; });
  CHECK(done);
  CHECK(sink.reports.empty());
}

TEST_CASE("A live channel's reports carry its LiveStreamId to the end and say it cannot seek") {
  RecordingSink sink;
  PlaybackReporter reporter{sink};
  reporter.setProgressInterval(std::chrono::hours(1));
  core::EpochCounter<core::MediaEpochTag> files;
  const core::MediaEpoch epoch = files.advance();
  api::PlaybackStream stream;
  stream.itemId = QStringLiteral("channel");
  stream.mediaSourceId = QStringLiteral("source");
  stream.playSessionId = QStringLiteral("play");
  stream.liveStreamId = QStringLiteral("live-1");
  stream.isLive = true;
  reporter.begin(epoch, stream, std::nullopt, std::nullopt);
  core::SessionSnapshot snapshot;
  snapshot.media = epoch;
  snapshot.phase = PlaybackPhase::Playing;
  snapshot.position = *core::MediaTicks::fromTicks(Second);
  reporter.snapshotChanged(snapshot);
  reporter.end();
  REQUIRE(sink.events() == QStringList{QStringLiteral("start"), QStringLiteral("stopped")});
  for (const Report& r : sink.reports) {
    CHECK(r.progress.liveStreamId == QStringLiteral("live-1"));
    CHECK_FALSE(r.progress.canSeek);
  }

  // The next file is not live.
  const core::MediaEpoch next = files.advance();
  api::PlaybackStream file;
  file.itemId = QStringLiteral("movie");
  reporter.begin(next, file, std::nullopt, std::nullopt);
  snapshot.media = next;
  reporter.snapshotChanged(snapshot);
  REQUIRE(sink.reports.size() == 3);
  CHECK(sink.reports.back().progress.liveStreamId.isEmpty());
  CHECK(sink.reports.back().progress.canSeek);
}
