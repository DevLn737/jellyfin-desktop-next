#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QSignalSpy>

#include <functional>
#include <memory>

// What the session runtime does when playback fails (audit C4, I1): a
// stream that dies while it plays is reopened once through PlaybackInfo at
// its position; the group's item that cannot be played makes the player
// leave the group, so the others do not wait for it; group controls while
// the connection is lost say why nothing happens.

using namespace jfd;

namespace {

  bool waitUntil(const std::function<bool()>& done, int timeoutMs = 5000) {
    QDeadlineTimer deadline(timeoutMs);
    while (!done() && !deadline.hasExpired()) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
  }

  QJsonObject json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)).object(); }

  const char* PlaybackInfoJson = R"({
    "MediaSources": [{
      "Id": "src1", "Protocol": "File", "Path": "/media/film.mkv", "Container": "mkv",
      "SupportsDirectPlay": true, "SupportsDirectStream": true, "RunTimeTicks": 72000000000,
      "MediaStreams": [
        {"Index": 0, "Type": "Video", "Codec": "h264"},
        {"Index": 1, "Type": "Audio", "Codec": "aac"}
      ]
    }],
    "PlaySessionId": "ps1"
  })";

  struct Fixture {
    // PlaybackInfo answers 500 while this is set (the server restarts).
    bool serverDown = false;
    test::FakeServer server{[this](const test::HttpRequest& request) {
      test::HttpResponse response;
      if (request.path.endsWith(QLatin1String("/PlaybackInfo"))) {
        if (serverDown) {
          response.status = 500;
          return response;
        }
        response.body = PlaybackInfoJson;
      } else if (request.path == QLatin1String("/GetUtcTime")) {
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        response.body = QStringLiteral(R"({"RequestReceptionTime":"%1","ResponseTransmissionTime":"%1"})")
                            .arg(now)
                            .toUtf8();
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
                                                    QStringLiteral("CI"), QStringLiteral("jfd-room-recovery")}};
    api::SessionSocket socket{client};
    testing::VirtualBackend* backend = nullptr;
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<session::GroupModel> model;

    Fixture() {
      client.setServerUrl(server.url());
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [this](const core::Clock& clock, session::SessionRuntime::Post post) {
            auto made = std::make_unique<testing::VirtualBackend>(clock, std::move(post));
            backend = made.get();
            return made;
          },
          session::SessionRuntime::Settings{});
      model = std::make_unique<session::GroupModel>(*runtime, client, socket);
    }

    QList<test::HttpRequest> requests(const QString& suffix) const {
      QList<test::HttpRequest> found;
      for (const auto& request : server.requests())
        if (request.path.endsWith(suffix)) found.append(request);
      return found;
    }

    const core::SessionSnapshot& snapshot() const { return runtime->snapshot(); }

    void joined() {
      // The socket is up: the runtime measures the server's clock, without
      // which no Ready can say when it was.
      Q_EMIT socket.ready(1);
      REQUIRE(waitUntil([&] { return !requests(QStringLiteral("/GetUtcTime")).isEmpty(); }));
      Q_EMIT socket.syncPlayGroupUpdate(
          json(R"({"GroupId":"g1","Type":"GroupJoined","Data":{"GroupId":"g1","GroupName":"Movie night","State":"Idle"}})"));
      REQUIRE(waitUntil([&] { return model->inGroup(); }));
    }

    void groupPlays(const char* itemId) {
      const QString update = QStringLiteral(R"({"GroupId":"g1","Type":"PlayQueue","Data":{
        "Reason":"NewPlaylist","LastUpdate":"2026-10-20T07:00:00Z",
        "Playlist":[{"ItemId":"%1","PlaylistItemId":"p1"},{"ItemId":"next","PlaylistItemId":"p2"}],
        "PlayingItemIndex":0,"StartPositionTicks":0,"IsPlaying":false,"PlaybackRate":1}})")
                                 .arg(QLatin1String(itemId));
      Q_EMIT socket.syncPlayGroupUpdate(QJsonDocument::fromJson(update.toUtf8()).object());
    }

    static bool told(const QSignalSpy& notices, const char* text) {
      for (const auto& args : notices)
        if (args.at(0).toString().contains(QLatin1String(text)) && args.at(1).toBool()) return true;
      return false;
    }
  };

} // namespace

TEST_CASE("A stream that dies while playing is reopened at its position through PlaybackInfo") {
  Fixture f;
  f.runtime->play(QStringLiteral("film"), 0);
  REQUIRE(waitUntil([&] { return f.snapshot().phase == core::PlaybackPhase::Playing; }));
  REQUIRE(waitUntil([&] { return f.snapshot().position.milliseconds() >= 300; }));
  const auto firstEpoch = f.snapshot().media;
  REQUIRE(f.requests(QStringLiteral("/PlaybackInfo")).size() == 1);

  f.backend->failCurrent("connection reset");
  REQUIRE(waitUntil([&] { return f.requests(QStringLiteral("/PlaybackInfo")).size() == 2; }));
  const auto body = QJsonDocument::fromJson(f.requests(QStringLiteral("/PlaybackInfo")).back().body).object();
  CHECK(body.value(QStringLiteral("StartTimeTicks")).toDouble() >= 3'000'000);
  CHECK(body.value(QStringLiteral("MediaSourceId")).toString() == QStringLiteral("src1"));
  REQUIRE(waitUntil([&] { return f.snapshot().phase == core::PlaybackPhase::Playing; }));
  CHECK(f.snapshot().media != firstEpoch);
  CHECK(f.snapshot().position.milliseconds() >= 300);
  CHECK(f.backend->loadedUrls.size() == 2);

  // Dying again at once: "Playback failed" with the stream's error.
  f.backend->failCurrent("connection reset again");
  REQUIRE(waitUntil([&] { return f.snapshot().phase == core::PlaybackPhase::Failed; }));
  CHECK(f.snapshot().error == "connection reset again");
  QCoreApplication::processEvents();
  CHECK(f.requests(QStringLiteral("/PlaybackInfo")).size() == 2);
}

TEST_CASE("A reopen the server cannot answer ends in Failed with the stream's error") {
  Fixture f;
  f.runtime->play(QStringLiteral("film"), 0);
  REQUIRE(waitUntil([&] { return f.snapshot().phase == core::PlaybackPhase::Playing; }));
  f.serverDown = true;
  f.backend->failCurrent("connection reset");
  REQUIRE(waitUntil([&] { return f.snapshot().phase == core::PlaybackPhase::Failed; }));
  CHECK(f.snapshot().error == "connection reset");
}

TEST_CASE("The group's item that fails in the player leaves the group and says why") {
  Fixture f;
  f.joined();
  QSignalSpy notices(f.model.get(), &session::GroupModel::notice);
  f.backend->failingLoads = 1;
  f.groupPlays("film");
  // Otherwise the whole group would wait for our Ready.
  REQUIRE(waitUntil([&] { return f.requests(QStringLiteral("/SyncPlay/Leave")).size() == 1; }));
  CHECK(Fixture::told(notices, "could not be played here"));
  CHECK(f.snapshot().phase == core::PlaybackPhase::Failed);
}

TEST_CASE("In a group a dying stream reports Buffering, reopens and reports Ready again") {
  Fixture f;
  f.joined();
  f.groupPlays("film");
  REQUIRE(waitUntil([&] { return f.requests(QStringLiteral("/SyncPlay/Ready")).size() == 1; }));
  f.backend->failCurrent("connection reset");
  REQUIRE(waitUntil([&] { return f.requests(QStringLiteral("/SyncPlay/Buffering")).size() == 1; }));
  REQUIRE(waitUntil([&] { return f.requests(QStringLiteral("/SyncPlay/Ready")).size() == 2; }));
  const auto ready = QJsonDocument::fromJson(f.requests(QStringLiteral("/SyncPlay/Ready")).back().body).object();
  CHECK(ready.value(QStringLiteral("PlaylistItemId")).toString() == QStringLiteral("p1"));
  CHECK(f.requests(QStringLiteral("/SyncPlay/Leave")).isEmpty());
  CHECK(f.model->inGroup());
}

TEST_CASE("Group controls while the connection is lost say so instead of vanishing") {
  Fixture f;
  f.joined();
  f.groupPlays("film");
  REQUIRE(waitUntil([&] { return f.requests(QStringLiteral("/SyncPlay/Ready")).size() == 1; }));
  QSignalSpy notices(f.model.get(), &session::GroupModel::notice);
  Q_EMIT f.socket.disconnected(QStringLiteral("gone"));
  REQUIRE(waitUntil([&] { return !f.model->connected(); }));
  f.runtime->post(core::intent::Pause{});
  REQUIRE(waitUntil([&] { return Fixture::told(notices, "No connection to the group"); }));
  CHECK(f.requests(QStringLiteral("/SyncPlay/Pause")).isEmpty());
}
