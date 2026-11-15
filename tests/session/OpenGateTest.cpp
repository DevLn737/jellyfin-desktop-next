#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/SessionRuntime.h>

#include <QCoreApplication>
#include <QDeadlineTimer>

#include <functional>
#include <memory>

// The launcher's open gate (the display's refresh rate switch, stage 3
// package 3.5): a resolved file opens only when the gate lets it, and a
// newer request or a cancel drops an open the gate still holds.

using namespace jfd;

namespace {

  bool waitUntil(const std::function<bool()>& done, int timeoutMs = 5000) {
    QDeadlineTimer deadline(timeoutMs);
    while (!done() && !deadline.hasExpired()) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
  }

  const char* PlaybackInfoJson = R"({
    "MediaSources": [{
      "Id": "src1", "Protocol": "File", "Path": "/media/film.mkv", "Container": "mkv",
      "SupportsDirectPlay": true, "SupportsDirectStream": true, "RunTimeTicks": 72000000000,
      "MediaStreams": [
        {"Index": 0, "Type": "Video", "Codec": "h264", "RealFrameRate": 23.976},
        {"Index": 1, "Type": "Audio", "Codec": "aac"}
      ]
    }],
    "PlaySessionId": "ps1"
  })";

  struct Gate {
    int calls = 0;
    QString itemId;
    std::optional<double> frameRate;
    std::function<void()> open;
  };

  struct Fixture {
    test::FakeServer server{[](const test::HttpRequest& request) {
      test::HttpResponse response;
      if (request.path.endsWith(QLatin1String("/PlaybackInfo"))) {
        response.body = PlaybackInfoJson;
      } else if (request.path.startsWith(QLatin1String("/Items/"))) {
        const QString id = request.path.mid(7);
        response.body = QStringLiteral(R"({"Id":"%1","Name":"Film %1","Type":"Movie","MediaType":"Video"})")
                            .arg(id)
                            .toUtf8();
      } else {
        response.status = 204;
      }
      return response;
    }};
    api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"),
                                                    QStringLiteral("CI"), QStringLiteral("jfd-open-gate")}};
    api::SessionSocket socket{client};
    std::unique_ptr<session::SessionRuntime> runtime;
    Gate gate;
    int resolved = 0;

    Fixture() {
      client.setServerUrl(server.url());
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
          },
          session::SessionRuntime::Settings{});
      runtime->setOpenGate([this](const session::PlaybackLauncher::Request& request, const api::Item&,
                                  const api::MediaSource& source, std::function<void()> open) {
        ++gate.calls;
        gate.itemId = request.itemId;
        gate.frameRate = source.mediaStreams.isEmpty() ? std::nullopt : source.mediaStreams.front().realFrameRate;
        gate.open = std::move(open);
      });
      QObject::connect(&runtime->launcher(), &session::PlaybackLauncher::resolved, [this] { ++resolved; });
    }

    session::PlaybackLauncher::Request request(const char* id) const {
      session::PlaybackLauncher::Request r;
      r.itemId = QLatin1String(id);
      return r;
    }
    core::PlaybackPhase phase() const { return runtime->snapshot().phase; }
  };

} // namespace

TEST_CASE("A resolved file waits at the gate and opens when the gate lets it") {
  Fixture f;
  f.runtime->launcher().open(f.request("film"));
  REQUIRE(waitUntil([&] { return f.gate.calls == 1; }));
  CHECK(f.gate.itemId == QStringLiteral("film"));
  REQUIRE(f.gate.frameRate.has_value());
  CHECK(*f.gate.frameRate == doctest::Approx(23.976));
  // Held: nothing is opened or announced yet.
  waitUntil([] { return false; }, 200);
  CHECK(f.resolved == 0);
  CHECK(f.phase() == core::PlaybackPhase::Idle);

  f.gate.open();
  CHECK(f.resolved == 1);
  REQUIRE(waitUntil([&] { return f.phase() != core::PlaybackPhase::Idle; }));
  CHECK(f.runtime->snapshot().title == "Film film");
}

TEST_CASE("A newer request or a cancel drops the open the gate holds") {
  Fixture f;
  f.runtime->launcher().open(f.request("first"));
  REQUIRE(waitUntil([&] { return f.gate.calls == 1; }));
  const std::function<void()> first = f.gate.open;
  f.runtime->launcher().open(f.request("second"));
  REQUIRE(waitUntil([&] { return f.gate.calls == 2; }));
  first();
  CHECK(f.resolved == 0);
  f.gate.open();
  CHECK(f.resolved == 1);
  REQUIRE(waitUntil([&] { return f.phase() != core::PlaybackPhase::Idle; }));
  CHECK(f.runtime->snapshot().title == "Film second");

  f.runtime->launcher().open(f.request("third"));
  REQUIRE(waitUntil([&] { return f.gate.calls == 3; }));
  f.runtime->launcher().cancel();
  f.gate.open();
  CHECK(f.resolved == 1);
}

TEST_CASE("Without a gate the file opens as soon as it is resolved") {
  Fixture f;
  f.runtime->setOpenGate({});
  f.runtime->launcher().open(f.request("film"));
  REQUIRE(waitUntil([&] { return f.resolved == 1; }));
  CHECK(f.gate.calls == 0);
}
