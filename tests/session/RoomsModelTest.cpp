#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/RoomsModel.h>
#include <jfd/session/SessionRuntime.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>
#include <QTranslator>

#include <chrono>
#include <memory>

// The rooms model (package 3.9) on the session runtime and a fake server:
// when the list is asked for, what an unchanged list does to the rows, the
// room card, the user's SyncPlayAccess and the refusals' texts.

using namespace jfd;
using namespace std::chrono_literals;

namespace {

  bool waitUntil(const std::function<bool()>& done, int timeoutMs = 5000) {
    QDeadlineTimer deadline(timeoutMs);
    while (!done() && !deadline.hasExpired()) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
  }

  void pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  QJsonObject json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)).object(); }

  // The log lines written while it lives.
  class LogCapture {
  public:
    LogCapture() {
      s_lines.clear();
      m_previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& text) {
        s_lines.append(text);
      });
    }
    ~LogCapture() { qInstallMessageHandler(m_previous); }
    bool contains(const QString& part) const {
      for (const QString& line : std::as_const(s_lines))
        if (line.contains(part)) return true;
      return false;
    }

  private:
    inline static QStringList s_lines;
    QtMessageHandler m_previous = nullptr;
  };

  struct Fixture {
    // What SyncPlay/List and Users/Me answer.
    QByteArray list = "[]";
    QString access = QStringLiteral("CreateAndJoinGroups");

    test::FakeServer server{[this](const test::HttpRequest& request) {
      test::HttpResponse response;
      if (request.path == QLatin1String("/SyncPlay/List")) {
        response.body = list;
        return response;
      }
      if (request.path == QLatin1String("/Users/Me")) {
        response.body = QStringLiteral(R"({"Id":"u1","Name":"alice","ServerId":"s1","Policy":{"SyncPlayAccess":"%1"}})")
                            .arg(access)
                            .toUtf8();
        return response;
      }
      response.status = 204;
      return response;
    }};
    api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"),
                                                    QStringLiteral("CI"), QStringLiteral("jfd-rooms-model")}};
    api::SessionSocket socket{client};
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<session::GroupModel> group;
    std::unique_ptr<session::RoomsModel> rooms;

    Fixture() {
      client.setServerUrl(server.url());
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
          },
          session::SessionRuntime::Settings{});
      group = std::make_unique<session::GroupModel>(*runtime, client, socket);
      rooms = std::make_unique<session::RoomsModel>(*group, client);
      // The model asks for the access on construction.
      REQUIRE(waitUntil([&] { return group->access() == access; }));
    }

    int count(const char* path) const {
      int n = 0;
      for (const auto& request : server.requests())
        if (request.path == QLatin1String(path)) ++n;
      return n;
    }

    void setAccess(const QString& value) {
      access = value;
      group->checkAccess();
      REQUIRE(waitUntil([&] { return group->access() == value; }));
    }

    // The server put us in group g1.
    void joined() {
      Q_EMIT socket.syncPlayGroupUpdate(
          json(R"({"GroupId":"g1","Type":"GroupJoined","Data":{"GroupId":"g1","GroupName":"Movie night","State":"Idle"}})"));
      REQUIRE(waitUntil([&] { return group->inGroup(); }));
    }
  };

  const QByteArray TwoRooms = R"([
    {"GroupId":"a","GroupName":"Movie night","State":"Idle","Participants":["alice"],"LastUpdatedAt":"2026-11-15T10:00:00Z"},
    {"GroupId":"b","GroupName":"Cartoons","State":"Playing","Participants":["bob","alice"],"LastUpdatedAt":"2026-11-15T10:00:00Z"}])";

} // namespace

TEST_CASE("The list is asked for at once when the tab shows, then every interval, never while hidden") {
  Fixture f;
  f.list = TwoRooms;

  // Hidden: 60 intervals (60 s at 1 s; here 20 ms each) and not one request.
  f.rooms->setPollInterval(20ms);
  pump(60 * 20);
  CHECK(f.count("/SyncPlay/List") == 0);
  CHECK(f.rooms->count() == 0);

  // Shown: the first request goes at once, not one interval later.
  f.rooms->setPollInterval(10s);
  f.rooms->setActive(true);
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/List") == 1; }, 2000));
  REQUIRE(waitUntil([&] { return f.rooms->count() == 2; }, 2000));
  CHECK(f.rooms->loaded());
  pump(300);
  CHECK(f.count("/SyncPlay/List") == 1);

  // Then one per interval while it stays shown.
  f.rooms->setActive(false);
  f.rooms->setPollInterval(50ms);
  const int before = f.count("/SyncPlay/List");
  f.rooms->setActive(true);
  pump(1000);
  const int polled = f.count("/SyncPlay/List") - before;
  CAPTURE(polled);
  CHECK(polled >= 5);
  CHECK(polled <= 25);

  // Hidden again (another tab, or the window lost the focus): nothing more.
  f.rooms->setActive(false);
  pump(100);
  const int stopped = f.count("/SyncPlay/List");
  pump(60 * 50);
  CHECK(f.count("/SyncPlay/List") == stopped);

  // Without a connection neither, and back online it asks at once.
  f.rooms->setPollInterval(10s);
  f.rooms->setOnline(false);
  f.rooms->setActive(true);
  pump(300);
  CHECK(f.count("/SyncPlay/List") == stopped);
  f.rooms->setOnline(true);
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/List") == stopped + 1; }, 2000));
}

TEST_CASE("An unchanged list leaves the rows alone; a change touches only its row") {
  Fixture f;
  f.list = TwoRooms;
  f.rooms->refresh();
  REQUIRE(waitUntil([&] { return f.rooms->count() == 2 && !f.rooms->loading(); }));
  CHECK(f.rooms->get(0).value(QStringLiteral("name")).toString() == QStringLiteral("Movie night"));
  CHECK(f.rooms->get(1).value(QStringLiteral("state")).toString() == QStringLiteral("playing"));
  CHECK(f.rooms->get(1).value(QStringLiteral("participantCount")).toInt() == 2);

  const QPersistentModelIndex a = f.rooms->index(0);
  const QPersistentModelIndex b = f.rooms->index(1);
  QSignalSpy inserted(f.rooms.get(), &QAbstractItemModel::rowsInserted);
  QSignalSpy removed(f.rooms.get(), &QAbstractItemModel::rowsRemoved);
  QSignalSpy moved(f.rooms.get(), &QAbstractItemModel::rowsMoved);
  QSignalSpy changed(f.rooms.get(), &QAbstractItemModel::dataChanged);
  QSignalSpy reset(f.rooms.get(), &QAbstractItemModel::modelReset);

  // The same list again (the next poll): no signal at all.
  f.rooms->refresh();
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/List") == 2 && !f.rooms->loading(); }));
  CHECK(inserted.isEmpty());
  CHECK(removed.isEmpty());
  CHECK(moved.isEmpty());
  CHECK(changed.isEmpty());
  CHECK(reset.isEmpty());

  // Someone left room b: only its row changes.
  f.list = R"([
    {"GroupId":"a","GroupName":"Movie night","State":"Idle","Participants":["alice"]},
    {"GroupId":"b","GroupName":"Cartoons","State":"Paused","Participants":["bob"]}])";
  f.rooms->refresh();
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/List") == 3 && !f.rooms->loading(); }));
  REQUIRE(changed.size() == 1);
  CHECK(changed.front().at(0).value<QModelIndex>().row() == 1);
  CHECK(inserted.isEmpty());
  CHECK(removed.isEmpty());
  CHECK(b.isValid());
  CHECK(b.row() == 1);
  CHECK(b.data(session::RoomsModel::StateRole).toString() == QStringLiteral("paused"));

  // Room a closed and c opened before b: b's row moves, it is not made again.
  changed.clear();
  f.list = R"([
    {"GroupId":"c","GroupName":"Concert","State":"Waiting","Participants":["carol"]},
    {"GroupId":"b","GroupName":"Cartoons","State":"Paused","Participants":["bob"]}])";
  f.rooms->refresh();
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/List") == 4 && !f.rooms->loading(); }));
  CHECK(removed.size() == 1);
  CHECK(inserted.size() == 1);
  CHECK(changed.isEmpty());
  CHECK(reset.isEmpty());
  CHECK_FALSE(a.isValid());
  CHECK(b.isValid());
  CHECK(b.row() == 1);
  CHECK(f.rooms->indexOf(QStringLiteral("c")) == 0);
}

TEST_CASE("The room card shows the members and the state, and says when the room closed") {
  Fixture f;
  f.list = TwoRooms;
  f.rooms->refresh();
  REQUIRE(waitUntil([&] { return f.rooms->count() == 2; }));
  QSignalSpy selected(f.rooms.get(), &session::RoomsModel::selectedChanged);
  f.rooms->select(QStringLiteral("b"));
  CHECK(selected.size() == 1);
  CHECK(f.rooms->selected().value(QStringLiteral("participants")).toStringList() ==
        QStringList{QStringLiteral("bob"), QStringLiteral("alice")});
  CHECK(f.rooms->selected().value(QStringLiteral("state")).toString() == QStringLiteral("playing"));
  CHECK_FALSE(f.rooms->selectedGone());

  // An unchanged list does not touch the card; a closed room does.
  f.rooms->refresh();
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/List") == 2 && !f.rooms->loading(); }));
  CHECK(selected.size() == 1);
  f.list = R"([{"GroupId":"a","GroupName":"Movie night","State":"Idle","Participants":["alice"]}])";
  f.rooms->refresh();
  REQUIRE(waitUntil([&] { return f.rooms->count() == 1; }));
  CHECK(selected.size() == 2);
  CHECK(f.rooms->selectedGone());
  CHECK(f.rooms->selected().isEmpty());

  // Join from the card goes through the group model.
  f.rooms->join(QStringLiteral("a"));
  REQUIRE(waitUntil([&] { return f.count("/SyncPlay/Join") == 1; }));
  CHECK(f.group->busy());
  QJsonObject body;
  for (const auto& request : f.server.requests())
    if (request.path == QLatin1String("/SyncPlay/Join")) body = QJsonDocument::fromJson(request.body).object();
  CHECK(body.value(QStringLiteral("GroupId")).toString() == QStringLiteral("a"));
}

TEST_CASE("SyncPlayAccess: everything, joining only, or nothing, also by the other ways in") {
  Fixture f;
  f.list = TwoRooms;
  QSignalSpy access(f.rooms.get(), &session::RoomsModel::accessChanged);

  SUBCASE("CreateAndJoinGroups: list, join and create") {
    CHECK(f.rooms->available());
    CHECK(f.rooms->canJoin());
    CHECK(f.rooms->canCreate());
    f.rooms->create(QStringLiteral("Movie night"));
    REQUIRE(waitUntil([&] { return f.count("/SyncPlay/New") == 1; }));
  }

  SUBCASE("JoinGroups: list and join, no create") {
    f.setAccess(QStringLiteral("JoinGroups"));
    CHECK(access.size() >= 1);
    CHECK(f.rooms->available());
    CHECK(f.rooms->canJoin());
    CHECK_FALSE(f.rooms->canCreate());
    f.rooms->create(QStringLiteral("Movie night"));
    // Watch together on an item would create one too.
    QSignalSpy notices(f.group.get(), &session::GroupModel::notice);
    f.group->watchTogether(QStringLiteral("movie"), QStringLiteral("Movie night"));
    CHECK(notices.size() == 1);
    pump(200);
    CHECK(f.count("/SyncPlay/New") == 0);
    CHECK(f.group->errorText() == session::GroupModel::refusalText(QStringLiteral("CreateGroupDenied")));
    f.rooms->setActive(true);
    REQUIRE(waitUntil([&] { return f.rooms->count() == 2; }));
    f.rooms->join(QStringLiteral("a"));
    REQUIRE(waitUntil([&] { return f.count("/SyncPlay/Join") == 1; }));
    // In a room, Watch together and the phone's Play play there.
    f.joined();
    f.group->playInGroup({QStringLiteral("x")}, 0, 0);
    REQUIRE(waitUntil([&] { return f.count("/SyncPlay/SetNewQueue") == 1; }));
  }

  SUBCASE("None: no list, no join, no create, no play in a group") {
    f.rooms->setActive(true);
    REQUIRE(waitUntil([&] { return f.rooms->count() == 2; }));
    f.rooms->select(QStringLiteral("a"));
    // The policy changes (an administrator took the right away).
    f.setAccess(QStringLiteral("None"));
    CHECK_FALSE(f.rooms->available());
    CHECK_FALSE(f.rooms->canJoin());
    CHECK_FALSE(f.rooms->canCreate());
    CHECK(f.rooms->count() == 0);
    CHECK(f.rooms->selectedId().isEmpty());
    const int listed = f.count("/SyncPlay/List");
    f.rooms->setPollInterval(20ms);
    f.rooms->setActive(false);
    f.rooms->setActive(true);
    f.rooms->refresh();
    pump(300);
    CHECK(f.count("/SyncPlay/List") == listed);

    f.rooms->join(QStringLiteral("a"));
    f.rooms->create(QStringLiteral("Movie night"));
    f.group->watchTogether(QStringLiteral("movie"), QStringLiteral("Movie night"));
    CHECK(f.group->errorText().length() > 0);
    // The phone's Play in a group, and its Play next, send nothing.
    f.joined();
    f.group->playInGroup({QStringLiteral("x")}, 0, 0);
    f.group->queueInGroup({QStringLiteral("x")}, true);
    pump(300);
    CHECK(f.count("/SyncPlay/Join") == 0);
    CHECK(f.count("/SyncPlay/New") == 0);
    CHECK(f.count("/SyncPlay/SetNewQueue") == 0);
    CHECK(f.count("/SyncPlay/Queue") == 0);
    CHECK_FALSE(f.group->busy());
  }

  SUBCASE("Unknown values count as full access") {
    f.setAccess(QStringLiteral("SomethingNew"));
    CHECK(f.rooms->canCreate());
  }
}

TEST_CASE("Each refusal from the server has its own text; an unknown one a general text and a log line") {
  Fixture f;
  const char* types[] = {"JoinGroupDenied", "GroupDoesNotExist", "LibraryAccessDenied", "NotInGroup",
                         "CreateGroupDenied"};
  QStringList texts;
  for (const char* type : types) {
    const std::string typeName = type;
    CAPTURE(typeName);
    const QString name = QString::fromLatin1(type);
    f.rooms->join(QStringLiteral("a"));
    REQUIRE(f.group->busy());
    Q_EMIT f.socket.syncPlayGroupUpdate(
        QJsonObject{{QStringLiteral("GroupId"), QStringLiteral("a")}, {QStringLiteral("Type"), name},
                    {QStringLiteral("Data"), QString()}});
    CHECK_FALSE(f.group->busy());
    const QString text = session::GroupModel::refusalText(name);
    CHECK_FALSE(text.isEmpty());
    CHECK(f.group->errorText() == text);
    CHECK_FALSE(texts.contains(text));
    texts.append(text);
  }

  // A type this client does not know, while waiting: the general text.
  CHECK(session::GroupModel::refusalText(QStringLiteral("SyncPlayRoomFull")).isEmpty());
  f.rooms->join(QStringLiteral("a"));
  REQUIRE(f.group->busy());
  LogCapture log;
  Q_EMIT f.socket.syncPlayGroupUpdate(json(R"({"GroupId":"a","Type":"SyncPlayRoomFull","Data":""})"));
  CHECK_FALSE(f.group->busy());
  CHECK(f.group->errorText() == session::GroupModel::unknownRefusalText());
  CHECK(log.contains(QStringLiteral("SyncPlayRoomFull")));

  // Known updates that are not refusals change nothing here.
  Q_EMIT f.socket.syncPlayGroupUpdate(json(R"({"GroupId":"a","Type":"UserJoined","Data":"bob"})"));
  CHECK(f.group->errorText() == session::GroupModel::unknownRefusalText());

  // And each is translated.
  QTranslator russian;
  REQUIRE(russian.load(QStringLiteral(":/i18n/session_ru.qm")));
  QCoreApplication::installTranslator(&russian);
  QStringList translated;
  for (const char* type : types) {
    const std::string typeName = type;
    CAPTURE(typeName);
    const QString text = session::GroupModel::refusalText(QString::fromLatin1(type));
    CHECK_FALSE(texts.contains(text));
    CHECK_FALSE(translated.contains(text));
    translated.append(text);
  }
  CHECK(session::GroupModel::unknownRefusalText() != QStringLiteral("The server refused. Try again later."));
  QCoreApplication::removeTranslator(&russian);
}
