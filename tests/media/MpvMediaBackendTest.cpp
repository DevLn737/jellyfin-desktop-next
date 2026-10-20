#include <doctest/doctest.h>

#include "MpvMediaBackend.h"
#include "MpvPlayer.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
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
    MpvPlayer player {headless()};
    SteadyClock clock;
    std::vector<SessionInput> inputs;
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
