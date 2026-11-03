#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QSignalSpy>

#include <memory>

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
    test::FakeServer server{[](const test::HttpRequest& request) {
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
  CHECK(f.model->errorText() == QStringLiteral("You may not create groups on this server."));

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
