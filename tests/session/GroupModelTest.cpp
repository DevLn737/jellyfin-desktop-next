#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>

#include <functional>
#include <memory>
#include <optional>

// The group model on the session runtime and a fake server: what the server
// answers to a create or a group item, and what the screen shows.

using namespace jfd;

namespace {

  bool waitUntil(const std::function<bool()>& done, int timeoutMs = 5000) {
    QDeadlineTimer deadline(timeoutMs);
    while (!done() && !deadline.hasExpired()) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
  }

  QJsonObject json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)).object(); }

  struct Fixture {
    // A test's own answers, before the defaults below.
    std::function<std::optional<test::HttpResponse>(const test::HttpRequest&)> route;
    test::FakeServer server{[this](const test::HttpRequest& request) {
      if (route)
        if (auto answer = route(request)) return *answer;
      test::HttpResponse response;
      // Items the group asks for are gone.
      if (request.path.contains(QLatin1String("/Items"))) {
        response.status = 404;
        return response;
      }
      response.status = 204;
      return response;
    }};
    api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"),
                                                    QStringLiteral("CI"), QStringLiteral("jfd-group-model")}};
    api::SessionSocket socket{client};
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<session::GroupModel> model;

    Fixture() {
      client.setServerUrl(server.url());
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
          },
          session::SessionRuntime::Settings{});
      model = std::make_unique<session::GroupModel>(*runtime, client, socket);
    }

    int count(const char* path) const {
      int n = 0;
      for (const auto& request : server.requests())
        if (request.path == QLatin1String(path)) ++n;
      return n;
    }

    // The server put us in group g1.
    void joined() {
      Q_EMIT socket.syncPlayGroupUpdate(
          json(R"({"GroupId":"g1","Type":"GroupJoined","Data":{"GroupId":"g1","GroupName":"Movie night","State":"Idle"}})"));
      REQUIRE(waitUntil([&] { return model->inGroup(); }));
    }
  };

} // namespace

TEST_CASE("CreateGroupDenied ends the busy state and drops the queued item") {
  Fixture f;
  f.model->watchTogether(QStringLiteral("movie"), QStringLiteral("Movie night"));
  CHECK(f.model->busy());
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/New") == 1; }));
  // A user without the right to create groups: 204, then the refusal.
  QCoreApplication::processEvents();
  CHECK(f.model->busy());
  Q_EMIT f.socket.syncPlayGroupUpdate(json(R"({"GroupId":"","Type":"CreateGroupDenied","Data":""})"));
  CHECK_FALSE(f.model->busy());
  CHECK(f.model->errorText() == QStringLiteral("You may not create rooms on this server."));

  // Joining another group later does not start the refused item there.
  f.joined();
  QCoreApplication::processEvents();
  CHECK(f.count("/SyncPlay/SetNewQueue") == 0);
}

TEST_CASE("NotInGroup ends a pending request") {
  Fixture f;
  f.model->joinGroup(QStringLiteral("g1"));
  CHECK(f.model->busy());
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/Join") == 1; }));
  Q_EMIT f.socket.syncPlayGroupUpdate(json(R"({"GroupId":"","Type":"NotInGroup","Data":""})"));
  CHECK_FALSE(f.model->busy());
  CHECK_FALSE(f.model->errorText().isEmpty());
}

TEST_CASE("A group item that cannot be opened leaves the group and says why") {
  Fixture f;
  f.joined();
  QSignalSpy notices(f.model.get(), &session::GroupModel::notice);
  Q_EMIT f.socket.syncPlayGroupUpdate(json(R"({"GroupId":"g1","Type":"PlayQueue","Data":{
    "Reason":"NewPlaylist","LastUpdate":"2026-10-20T07:00:00Z",
    "Playlist":[{"ItemId":"gone","PlaylistItemId":"p1"}],"PlayingItemIndex":0,
    "StartPositionTicks":0,"IsPlaying":false,"PlaybackRate":1}})"));
  // Otherwise the whole group would wait for our Ready.
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/Leave") == 1; }));
  bool told = false;
  for (const auto& args : notices)
    if (args.at(0).toString().contains(QLatin1String("could not be opened")) && args.at(1).toBool()) told = true;
  CHECK(told);
}

TEST_CASE("A group item to open, whoever started it, asks the app to show the player") {
  Fixture f;
  QSignalSpy opening(f.model.get(), &session::GroupModel::itemOpening);
  f.joined();
  CHECK(opening.count() == 0);
  // Another member (or our own Play) set a new queue: the item opens here.
  Q_EMIT f.socket.syncPlayGroupUpdate(json(R"({"GroupId":"g1","Type":"PlayQueue","Data":{
    "Reason":"NewPlaylist","LastUpdate":"2026-10-20T07:00:00Z",
    "Playlist":[{"ItemId":"movie","PlaylistItemId":"p1"}],"PlayingItemIndex":0,
    "StartPositionTicks":0,"IsPlaying":false,"PlaybackRate":1}})"));
  REQUIRE(waitUntil([&] { return opening.count() == 1; }));
}

TEST_CASE("Remote control's Play in a group asks the group for a new queue or adds to it") {
  Fixture f;
  // Outside a group nothing is sent.
  f.model->playInGroup({QStringLiteral("a")}, 0, 0);
  f.model->queueInGroup({QStringLiteral("a")}, true);
  QCoreApplication::processEvents();
  CHECK(f.count("/SyncPlay/SetNewQueue") == 0);
  CHECK(f.count("/SyncPlay/Queue") == 0);

  f.joined();
  f.model->playInGroup({QStringLiteral("a"), QStringLiteral("b")}, 1, 50'000'000);
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/SetNewQueue") == 1; }));
  f.model->queueInGroup({QStringLiteral("c")}, true);
  f.model->queueInGroup({QStringLiteral("d")}, false);
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/Queue") == 2; }));

  QList<QJsonObject> queued;
  for (const auto& request : f.server.requests()) {
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    if (request.path == QLatin1String("/SyncPlay/SetNewQueue")) {
      CHECK(body.value(QStringLiteral("PlayingQueue")).toArray().size() == 2);
      CHECK(body.value(QStringLiteral("PlayingItemPosition")).toInt() == 1);
      CHECK(body.value(QStringLiteral("StartPositionTicks")).toDouble() == 50'000'000.0);
    } else if (request.path == QLatin1String("/SyncPlay/Queue")) {
      queued.append(body);
    }
  }
  REQUIRE(queued.size() == 2);
  CHECK(queued.at(0).value(QStringLiteral("Mode")).toString() == QStringLiteral("QueueNext"));
  CHECK(queued.at(0).value(QStringLiteral("ItemIds")).toArray().at(0).toString() == QStringLiteral("c"));
  CHECK(queued.at(1).value(QStringLiteral("Mode")).toString() == QStringLiteral("Queue"));
  CHECK(f.model->errorText().isEmpty());
}

TEST_CASE("A failed automatic rejoin shows no join error; a failed join the user asked for does") {
  Fixture f;
  // The rejoin after a lost connection uses the same request; the group
  // client retries it and reports only the last failure.
  Q_EMIT f.runtime->transport().replied(QStringLiteral("/SyncPlay/Join"), 500);
  CHECK(f.model->errorText().isEmpty());

  f.model->joinGroup(QStringLiteral("g1"));
  Q_EMIT f.runtime->transport().replied(QStringLiteral("/SyncPlay/Join"), 500);
  CHECK_FALSE(f.model->busy());
  CHECK(f.model->errorText() == QStringLiteral("Could not join the group."));
}

TEST_CASE("With the Room Members plugin the room shows where each member is and their ping") {
  Fixture f;
  f.route = [](const test::HttpRequest& request) -> std::optional<test::HttpResponse> {
    if (request.path != QLatin1String("/RoomMembers")) return std::nullopt;
    test::HttpResponse response;
    // Bob reported 2:00 playing 5 s before the answer; we are at 1:58, paused.
    response.body = R"({"GroupId":"g1","ServerTimeUtc":"2026-11-26T10:00:05.0000000Z","Members":[
      {"UserName":"alice","DeviceName":"pc","IsSelf":true,"PingMs":40,"IsBuffering":false,
       "ItemId":"m1","PositionTicks":1180000000,"IsPaused":true,"PositionUpdatedUtc":"2026-11-26T10:00:00.0000000Z"},
      {"UserName":"bob","DeviceName":"tv","IsSelf":false,"PingMs":120,"IsBuffering":false,
       "ItemId":"m1","PositionTicks":1200000000,"IsPaused":false,"PositionUpdatedUtc":"2026-11-26T10:00:00.0000000Z"},
      {"UserName":"carol","DeviceName":"phone","IsSelf":false,"PingMs":300,"IsBuffering":true}]})";
    return response;
  };
  QSignalSpy changed(f.model.get(), &session::GroupModel::membersChanged);
  CHECK(f.model->members().isEmpty());
  f.joined();
  REQUIRE(waitUntil([&] { return f.model->members().size() == 3; }));
  CHECK(f.model->membersSupported());
  const QVariantList members = f.model->members();
  const QVariantMap self = members.at(0).toMap();
  CHECK(self.value(QStringLiteral("self")).toBool());
  CHECK(self.value(QStringLiteral("positionText")).toString() == QStringLiteral("1:58"));
  CHECK_FALSE(self.value(QStringLiteral("hasOffset")).toBool());
  const QVariantMap bob = members.at(1).toMap();
  CHECK(bob.value(QStringLiteral("pingMs")).toInt() == 120);
  // 2:00 plus the 5 s since the report (and the moments since the answer).
  CHECK(bob.value(QStringLiteral("positionMs")).toLongLong() >= 125'000);
  CHECK(bob.value(QStringLiteral("positionMs")).toLongLong() < 127'000);
  CHECK(bob.value(QStringLiteral("hasOffset")).toBool());
  CHECK(bob.value(QStringLiteral("offsetMs")).toLongLong() >= 7'000);
  const QVariantMap carol = members.at(2).toMap();
  CHECK(carol.value(QStringLiteral("buffering")).toBool());
  CHECK_FALSE(carol.value(QStringLiteral("hasPosition")).toBool());

  // Leaving the group forgets them.
  Q_EMIT f.socket.syncPlayGroupUpdate(json(R"({"GroupId":"g1","Type":"GroupLeft","Data":"g1"})"));
  REQUIRE(waitUntil([&] { return !f.model->inGroup(); }));
  CHECK(f.model->members().isEmpty());
}

TEST_CASE("Without the Room Members plugin the room keeps names only and stops asking") {
  Fixture f;
  f.route = [](const test::HttpRequest& request) -> std::optional<test::HttpResponse> {
    if (request.path != QLatin1String("/RoomMembers")) return std::nullopt;
    test::HttpResponse response;
    response.status = 404;
    response.body = {};
    return response;
  };
  f.joined();
  REQUIRE(waitUntil([&] { return f.count("/RoomMembers") == 1; }));
  // Several polling periods pass without another request.
  QDeadlineTimer later(3500);
  while (!later.hasExpired()) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  CHECK(f.count("/RoomMembers") == 1);
  CHECK_FALSE(f.model->membersSupported());
  CHECK(f.model->members().isEmpty());
}
