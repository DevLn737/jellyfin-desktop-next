#include <doctest/doctest.h>

#include "MpvMediaBackend.h"
#include "MpvPlayer.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>

#include <cmath>
#include <variant>
#include <vector>

using namespace jfd;
using namespace jfd::core;
using jfd::media::MpvMediaBackend;
using jfd::media::MpvPlayer;

namespace {

  // Headless mpv: no video output, no audio device, and a generated 30 s
  // clip, so the test needs no files and no display.
  constexpr const char* Clip = "av://lavfi:testsrc=size=160x90:rate=25:duration=30";

  MpvPlayer::Options headless() {
    MpvPlayer::Options options;
    options.audioOutput = QStringLiteral("null");
    options.extra = {{"vo", "null"}, {"hr-seek", "yes"}};
    return options;
  }

  class SteadyClock final : public Clock {
  public:
    SteadyClock() { m_timer.start(); }
    LocalInstant now() const override {
      return LocalInstant::fromSinceOrigin(WallTicks(m_timer.nsecsElapsed() / 100));
    }

  private:
    QElapsedTimer m_timer;
  };

  struct Fixture {
    explicit Fixture(const MpvPlayer::Options& options = headless()) : player(options) {}
    MpvPlayer player;
    SteadyClock clock;
    std::vector<SessionInput> inputs;
    // Every input next() has gone through, for checks that something never came.
    std::vector<SessionInput> seen;
    MpvMediaBackend backend {player, clock, [this](SessionInput input) { inputs.push_back(std::move(input)); }};
    EpochCounter<MediaEpochTag> files;
    EpochCounter<RequestTag> requests;

    // Spins the event loop until an input of type T for `epoch` arrives
    // (and `accept` agrees), and returns it. Inputs before it are consumed.
    template <class T, class Accept>
    std::optional<T> next(MediaEpoch epoch, Accept accept, int timeoutMs = 10000) {
      QDeadlineTimer deadline(timeoutMs);
      for (;;) {
        while (!inputs.empty()) {
          SessionInput input = std::move(inputs.front());
          inputs.erase(inputs.begin());
          seen.push_back(input);
          if (const T* event = std::get_if<T>(&input); event && event->epoch == epoch && accept(*event))
            return *event;
        }
        if (deadline.hasExpired())
          return std::nullopt;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      }
    }

    template <class T> std::optional<T> next(MediaEpoch epoch) {
      return next<T>(epoch, [](const T&) { return true; });
    }
  };

  double positionOf(const backend::Sample& sample) {
    return sample.position ? sample.position->value : NAN;
  }

} // namespace

TEST_CASE("A file is held after opening and starts at the requested position") {
  Fixture f;
  REQUIRE(f.player.isValid());
  const MediaEpoch epoch = f.files.advance();
  f.backend.load(epoch, {Clip, false, true});

  const auto opened = f.next<backend::SourceOpened>(epoch);
  REQUIRE(opened);
  REQUIRE(opened->demuxerStart);
  CHECK(*opened->demuxerStart == doctest::Approx(0.0));
  // lavfi has no container duration; a real file reports one here.

  // Held: nothing plays until start().
  CHECK_FALSE(f.next<backend::PlaybackRestarted>(epoch, [](auto&) { return true; }, 500));

  f.backend.start(epoch, BackendSeconds {12.0});
  const auto restarted = f.next<backend::PlaybackRestarted>(epoch);
  REQUIRE(restarted);
  CHECK(restarted->sample.paused);
  CHECK(positionOf(restarted->sample) == doctest::Approx(12.0).epsilon(0.01));
}

TEST_CASE("An exact seek is reported as started, then restarted at the target") {
  Fixture f;
  const MediaEpoch epoch = f.files.advance();
  f.backend.load(epoch, {Clip, false, true});
  REQUIRE(f.next<backend::SourceOpened>(epoch));
  f.backend.start(epoch, std::nullopt);
  REQUIRE(f.next<backend::PlaybackRestarted>(epoch));

  f.backend.seek(epoch, f.requests.advance(), BackendSeconds {21.52});
  REQUIRE(f.next<backend::SeekStarted>(epoch));
  const auto restarted = f.next<backend::PlaybackRestarted>(epoch);
  REQUIRE(restarted);
  CHECK(positionOf(restarted->sample) == doctest::Approx(21.52).epsilon(0.002));

  // Playing at 2x moves the position on; Progress reports it.
  f.backend.setRate(epoch, *PlaybackRate::effective(2.0));
  f.backend.setPaused(epoch, false);
  const auto progress = f.next<backend::Progress>(epoch, [](const backend::Progress& p) {
    return !p.sample.paused && positionOf(p.sample) > 22.5;
  });
  REQUIRE(progress);
  CHECK(f.player.property("speed").toDouble() == doctest::Approx(2.0));
}

TEST_CASE("Failed requests and stale epochs are reported, not dropped") {
  Fixture f;
  const MediaEpoch epoch = f.files.advance();
  f.backend.load(epoch, {Clip, false, true});
  REQUIRE(f.next<backend::SourceOpened>(epoch));
  f.backend.start(epoch, std::nullopt);
  REQUIRE(f.next<backend::PlaybackRestarted>(epoch));

  const RequestId missing = f.requests.advance();
  f.backend.addExternalTrack(epoch, missing, {TrackKind::Subtitle, "/nonexistent/subtitles.srt", "x", "en"});
  const auto failed = f.next<backend::RequestFailed>(epoch);
  REQUIRE(failed);
  CHECK(failed->request == missing);

  const MediaEpoch stale = f.files.advance();
  const RequestId seek = f.requests.advance();
  f.backend.seek(stale, seek, BackendSeconds {1.0});
  const auto refused = f.next<backend::RequestFailed>(stale);
  REQUIRE(refused);
  CHECK(refused->request == seek);
}

TEST_CASE("Loading over a held file ends it and tags events with the new epoch") {
  Fixture f;
  const MediaEpoch first = f.files.advance();
  f.backend.load(first, {Clip, false, true});
  REQUIRE(f.next<backend::SourceOpened>(first));

  // Replaced while still held in the hook.
  const MediaEpoch second = f.files.advance();
  f.backend.load(second, {Clip, false, true});
  const auto ended = f.next<backend::Ended>(first);
  REQUIRE(ended);
  CHECK(ended->reason == backend::EndReason::Stopped);
  REQUIRE(f.next<backend::SourceOpened>(second));
  f.backend.start(second, BackendSeconds {5.0});
  const auto restarted = f.next<backend::PlaybackRestarted>(second);
  REQUIRE(restarted);
  CHECK(positionOf(restarted->sample) == doctest::Approx(5.0).epsilon(0.01));

  f.backend.stop(second);
  const auto stopped = f.next<backend::Ended>(second);
  REQUIRE(stopped);
  CHECK(stopped->reason == backend::EndReason::Stopped);
}

TEST_CASE("A file that cannot be opened ends with an error") {
  Fixture f;
  const MediaEpoch epoch = f.files.advance();
  f.backend.load(epoch, {"/nonexistent/movie.mkv", false, true});
  const auto ended = f.next<backend::Ended>(epoch);
  REQUIRE(ended);
  CHECK(ended->reason == backend::EndReason::Error);
}

TEST_CASE("Settings reach mpv as properties while it runs") {
  MpvPlayer player {headless()};
  player.setStringProperty("hwdec", QByteArrayLiteral("no"));
  player.setDoubleProperty("sub-font-size", 60);
  QDeadlineTimer deadline(5000);
  // hwdec is a list of modes.
  const auto hwdec = [&] { return player.property("hwdec").toStringList(); };
  while ((hwdec() != QStringList{QStringLiteral("no")} ||
          player.property("sub-font-size").toDouble() != 60) &&
         !deadline.hasExpired())
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  CHECK(hwdec() == QStringList{QStringLiteral("no")});
  CHECK(player.property("sub-font-size").toDouble() == doctest::Approx(60));
}

namespace {
  // Two short tones: a queued file has something to follow.
  constexpr const char* Tone = "av://lavfi:sine=frequency=440:duration=1.2";
  constexpr const char* Tone2 = "av://lavfi:sine=frequency=660:duration=1.2";

  template <class T> bool any(const std::vector<SessionInput>& inputs, MediaEpoch epoch) {
    for (const SessionInput& input : inputs)
      if (const T* event = std::get_if<T>(&input); event && event->epoch == epoch) return true;
    return false;
  }

  bool advancedTo(const std::vector<SessionInput>& inputs, MediaEpoch epoch) {
    for (const SessionInput& input : inputs)
      if (const auto* event = std::get_if<backend::Advanced>(&input); event && event->to == epoch)
        return true;
    return false;
  }
} // namespace

TEST_CASE("A queued file follows the current one by itself, reported as Advanced") {
  Fixture f;
  REQUIRE(f.player.isValid());
  const MediaEpoch first = f.files.advance();
  f.backend.load(first, {Tone, false, true});
  REQUIRE(f.next<backend::SourceOpened>(first));
  f.backend.start(first, std::nullopt);
  REQUIRE(f.next<backend::PlaybackRestarted>(first));
  f.backend.setPaused(first, false);

  const MediaEpoch second = f.files.advance();
  f.backend.preload(second, {Tone2, true, true});

  // The first file ends on its own; nothing says it ended, the second plays on.
  std::optional<backend::Advanced> advanced;
  QDeadlineTimer deadline(10000);
  while (!advanced && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    while (!advanced && !f.inputs.empty()) {
      SessionInput input = std::move(f.inputs.front());
      f.inputs.erase(f.inputs.begin());
      f.seen.push_back(input);
      if (const auto* event = std::get_if<backend::Advanced>(&input)) advanced = *event;
      if (const auto* ended = std::get_if<backend::Ended>(&input)) CHECK_FALSE(ended->epoch == first);
    }
  }
  REQUIRE(advanced);
  CHECK(advanced->from == first);
  CHECK(advanced->to == second);

  // Not held: it opens and restarts without a start() call.
  const auto opened = f.next<backend::SourceOpened>(second);
  REQUIRE(opened);
  REQUIRE(opened->demuxerStart);
  const auto restarted = f.next<backend::PlaybackRestarted>(second);
  REQUIRE(restarted);
  CHECK_FALSE(restarted->sample.paused);
  CHECK(positionOf(restarted->sample) < 0.5);
  CHECK_FALSE(any<backend::Ended>(f.seen, first));

  // The last file of the queue ends as usual.
  const auto ended = f.next<backend::Ended>(second);
  REQUIRE(ended);
  CHECK(ended->reason == backend::EndReason::EndOfFile);
}

TEST_CASE("A queued file is dropped by stop, load and a newer preload") {
  Fixture f;
  const MediaEpoch first = f.files.advance();
  f.backend.load(first, {Tone, false, true});
  REQUIRE(f.next<backend::SourceOpened>(first));
  f.backend.start(first, std::nullopt);
  REQUIRE(f.next<backend::PlaybackRestarted>(first));

  // A newer preload replaces the older one: only the newer is followed.
  const MediaEpoch older = f.files.advance();
  const MediaEpoch newer = f.files.advance();
  f.backend.preload(older, {Tone2, false, true});
  f.backend.preload(newer, {Tone2, false, true});
  f.backend.setPaused(first, false);
  REQUIRE(f.next<backend::SourceOpened>(newer));
  CHECK(advancedTo(f.seen, newer));
  CHECK_FALSE(advancedTo(f.seen, older));
  CHECK_FALSE(any<backend::SourceOpened>(f.seen, older));

  // stop() leaves nothing queued: the file just ends.
  f.backend.stop(newer);
  CHECK(f.next<backend::Ended>(newer));
  const MediaEpoch third = f.files.advance();
  f.backend.load(third, {Tone, false, true});
  REQUIRE(f.next<backend::SourceOpened>(third));
  f.backend.start(third, std::nullopt);
  REQUIRE(f.next<backend::PlaybackRestarted>(third));
  const MediaEpoch queued = f.files.advance();
  f.backend.preload(queued, {Tone2, false, true});
  const MediaEpoch replacing = f.files.advance();
  f.backend.load(replacing, {Tone, false, true});
  REQUIRE(f.next<backend::SourceOpened>(replacing));
  CHECK_FALSE(advancedTo(f.seen, queued));
  f.backend.start(replacing, std::nullopt);
  REQUIRE(f.next<backend::PlaybackRestarted>(replacing));
  f.backend.setPaused(replacing, false);
  const auto ended = f.next<backend::Ended>(replacing);
  REQUIRE(ended);
  CHECK(ended->reason == backend::EndReason::EndOfFile);
}

TEST_CASE("Two queued tones play without a gap") {
  const QString path = QDir::temp().filePath(QStringLiteral("jfd-gapless-%1.pcm").arg(QCoreApplication::applicationPid()));
  QFile::remove(path);
  MpvPlayer::Options options;
  options.audioOutput = QStringLiteral("pcm");
  options.extra = {{"vo", "null"}, {"ao-pcm-file", path.toUtf8()}, {"ao-pcm-waveheader", "no"},
                   {"audio-format", "s16"}, {"audio-samplerate", "48000"}, {"audio-channels", "mono"},
                   {"gapless-audio", "yes"}};
  {
    Fixture f(options);
    REQUIRE(f.player.isValid());
    const MediaEpoch first = f.files.advance();
    const MediaEpoch second = f.files.advance();
    f.backend.load(first, {Tone, false, true});
    REQUIRE(f.next<backend::SourceOpened>(first));
    f.backend.start(first, std::nullopt);
    REQUIRE(f.next<backend::PlaybackRestarted>(first));
    f.backend.preload(second, {Tone2, false, true});
    f.backend.setPaused(first, false);
    const auto ended = f.next<backend::Ended>(second, [](auto&) { return true; }, 15000);
    REQUIRE(ended);
    CHECK(advancedTo(f.seen, second));
  }

  QFile file(path);
  REQUIRE(file.open(QIODevice::ReadOnly));
  const QByteArray raw = file.readAll();
  file.close();
  QFile::remove(path);
  const auto* samples = reinterpret_cast<const std::int16_t*>(raw.constData());
  const qsizetype count = raw.size() / 2;
  // The 660 Hz tone starts where zero crossings come closer than the 440 Hz
  // tone's (54 samples against 36 apart at 48 kHz). Both tones are the same
  // length, so the second one played in full when the whole is twice the first
  // (without gapless audio the output stops short of it).
  qsizetype change = 0, lastCrossing = 0;
  for (qsizetype i = 1; i < count && change == 0; ++i) {
    if ((samples[i - 1] < 0) == (samples[i] < 0)) continue;
    if (lastCrossing > 0 && i - lastCrossing < 45) change = lastCrossing;
    lastCrossing = i;
  }
  CHECK(change > 48000);
  CHECK(count == doctest::Approx(2.0 * static_cast<double>(change)).epsilon(0.01));
  // The longest stretch of near-silence between 0.5 s and the end: a sine
  // crosses zero every ~1 ms, so anything over 5 ms is a hole.
  qsizetype longest = 0, run = 0;
  for (qsizetype i = 24000; i + 24000 < count; ++i) {
    if (std::abs(samples[i]) < 50) ++run; else run = 0;
    longest = std::max(longest, run);
  }
  CHECK(longest < 48 * 5);
}
