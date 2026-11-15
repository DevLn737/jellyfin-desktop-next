// The rooms tab (stage 3, package 3.9) in an offscreen window, on the real
// shell and a fake server: the live list with the keyboard alone (Tab,
// arrows, Enter, Esc), a room's card, joining and leaving; cards that are
// kept when their room did not change; the empty, offline and access
// states; and the room card's contrast and focus ring in both themes.
//
// JFD_UI_SCREENSHOTS=<dir> saves the tab in both themes at four window sizes.
#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/RoomsModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/GroupSession.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/RoomsSession.h>
#include <jfd/ui/SpatialNavigation.h>
#include <jfd/ui/TextEntry.h>
#include <jfd/ui/ThemePalette.h>

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_GroupPlugin)

using namespace jfd;
using namespace std::chrono_literals;
using jfd::test::waitFor;
using ui::SpatialNavigation;

namespace {

  constexpr int Timeout = 5000;

  const QByteArray TwoRooms = R"([
    {"GroupId":"a","GroupName":"Movie night","State":"Idle","Participants":["alice"]},
    {"GroupId":"b","GroupName":"Cartoons","State":"Playing","Participants":["bob","alice"]}])";

  QJsonObject json(const QByteArray& text) { return QJsonDocument::fromJson(text).object(); }

  QQuickItem* find(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    const QList<QQuickItem*> children = root->childItems();
    for (QQuickItem* child : children)
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  void pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  // A key as the keyboard, a remote or the gamepad (InputRouter) sends it.
  void tap(QQuickWindow& window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(&window, &release);
  }

  bool shown(QQuickItem* item) { return item != nullptr && item->isVisible(); }

  struct Scene {
    // What SyncPlay/List and Users/Me answer.
    QByteArray list = TwoRooms;
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
                                                    QStringLiteral("CI"), QStringLiteral("jfd-rooms-page")}};
    api::SessionSocket socket{client};
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<session::GroupModel> group;
    std::unique_ptr<session::RoomsModel> rooms;

    // After the models: the page goes first.
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> object;
    QQuickItem* shell = nullptr;
    ui::Navigator* navigator = nullptr;
    ui::TextEntry* entry = nullptr;
    QObject* theme = nullptr;

    explicit Scene(QSize size = QSize(1280, 800)) {
      client.setServerUrl(server.url());
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
          },
          session::SessionRuntime::Settings{});
      group = std::make_unique<session::GroupModel>(*runtime, client, socket);
      rooms = std::make_unique<session::RoomsModel>(*group, client);
      REQUIRE(waitFor([&] { return group->access() == access; }, Timeout));
      // The app polls every 5 s; the test does not wait that long.
      rooms->setPollInterval(100ms);
      ui::GroupSession::setInstance(group.get());
      ui::RoomsSession::setInstance(rooms.get());

      QQmlComponent import(&engine);
      import.setData("import QtQml\nimport Jfd.Ui.Shell\nQtObject {}", QUrl(QStringLiteral("qrc:/import.qml")));
      std::unique_ptr<QObject> holder(import.create());
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      REQUIRE(navigator != nullptr);
      theme = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme");
      REQUIRE(theme != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      navigator->registerPage(QStringLiteral("group"), QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Group/GroupPage.qml")));
      navigator->reset(QStringLiteral("home"));
      REQUIRE(navigator->navigate(QStringLiteral("group")));

      window.resize(size);
      QQmlComponent component(&engine);
      component.setData(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
AppShell {
  function setTvMode(on) { Theme.tvMode = on }
}
)",
                        QUrl(QStringLiteral("qrc:/rooms-shell.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      shell = qobject_cast<QQuickItem*>(object.get());
      shell->setParentItem(window.contentItem());
      shell->setSize(QSizeF(size));
      entry = window.findChild<ui::TextEntry*>(QStringLiteral("textEntry"));
      REQUIRE(entry != nullptr);
      window.show();
      window.requestActivate();
      REQUIRE(QTest::qWaitForWindowActive(&window));
      REQUIRE(waitFor([&] { return page() != nullptr && item("roomsPage") != nullptr; }, Timeout));
      pump(200);
    }

    ~Scene() {
      object.reset();
      theme->setProperty("tvMode", false);
      ui::RoomsSession::setInstance(nullptr);
      ui::GroupSession::setInstance(nullptr);
    }

    QQuickItem* page() const {
      QQuickItem* stack = find(window.contentItem(), QStringLiteral("pageStack"));
      return stack == nullptr ? nullptr : stack->property("currentItem").value<QQuickItem*>();
    }
    QQuickItem* item(const char* name) const { return find(page(), QString::fromLatin1(name)); }
    QQuickItem* card(const char* roomId) const { return item((QByteArray("room_") + roomId).constData()); }
    // The TextInput of an InputField.
    QQuickItem* input(const char* name) const {
      QQuickItem* field = item(name);
      return field ? field->property("field").value<QQuickItem*>() : nullptr;
    }

    // The named item that holds the focus (a field's TextInput is named by
    // its InputField).
    QString focusName() const {
      for (const QQuickItem* node = window.activeFocusItem(); node; node = node->parentItem())
        if (!node->objectName().isEmpty()) return node->objectName();
      return {};
    }

    int count(const char* path) const {
      int n = 0;
      for (const auto& request : server.requests())
        if (request.path == QLatin1String(path)) ++n;
      return n;
    }
    QJsonObject lastBody(const char* path) const {
      QJsonObject body;
      for (const auto& request : server.requests())
        if (request.path == QLatin1String(path)) body = QJsonDocument::fromJson(request.body).object();
      return body;
    }

    void joined(const char* id, const char* name) {
      Q_EMIT socket.syncPlayGroupUpdate(json(QStringLiteral(
          R"({"GroupId":"%1","Type":"GroupJoined","Data":{"GroupId":"%1","GroupName":"%2","State":"Idle","Participants":["alice"]}})")
          .arg(QLatin1String(id), QLatin1String(name)).toUtf8()));
      REQUIRE(waitFor([&] { return group->inGroup(); }, Timeout));
    }
    void left(const char* id) {
      Q_EMIT socket.syncPlayGroupUpdate(
          json(QStringLiteral(R"({"GroupId":"%1","Type":"GroupLeft","Data":"%1"})").arg(QLatin1String(id)).toUtf8()));
      REQUIRE(waitFor([&] { return !group->inGroup(); }, Timeout));
    }

  };

  double contrast(const QQuickItem* text, const QColor& background) {
    return ui::contrastRatio(text->property("color").value<QColor>(), background);
  }

} // namespace

TEST_CASE("The rooms tab with the keyboard alone: the list, a room's card, join and leave") {
  Scene scene;
  QQuickWindow& window = scene.window;

  // The tab shows in the active window: the list is asked for at once.
  CHECK(scene.rooms->active());
  REQUIRE(waitFor([&] { return shown(scene.card("a")) && shown(scene.card("b")); }, Timeout));
  CHECK(scene.card("b")->property("text").toString() == QStringLiteral("Cartoons"));
  CHECK(scene.card("b")->property("stateText").toString() == QStringLiteral("Playing"));
  CHECK(scene.card("b")->property("memberText").toString() == QStringLiteral("bob, alice"));

  // Another window active (or the app in the background): no polling.
  {
    QQuickWindow other;
    other.resize(200, 200);
    other.show();
    other.requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(&other));
    REQUIRE(waitFor([&] { return !scene.rooms->active(); }, Timeout));
    pump(150);
    const int listed = scene.count("/SyncPlay/List");
    pump(600);
    CHECK(scene.count("/SyncPlay/List") == listed);
  }
  window.requestActivate();
  REQUIRE(QTest::qWaitForWindowActive(&window));
  REQUIRE(waitFor([&] { return scene.rooms->active(); }, Timeout));

  // Tab: the name field, Create room, Refresh, then the rooms in order.
  REQUIRE(scene.input("roomNameField") != nullptr);
  scene.input("roomNameField")->forceActiveFocus(Qt::TabFocusReason);
  CHECK(scene.focusName() == QStringLiteral("roomNameField"));
  QStringList order;
  for (int i = 0; i < 4; ++i) {
    tap(window, Qt::Key_Tab);
    order.append(scene.focusName());
  }
  CHECK(order == QStringList{QStringLiteral("createButton"), QStringLiteral("refreshButton"),
                             QStringLiteral("room_a"), QStringLiteral("room_b")});
  CHECK(scene.card("b")->property("visualFocus").toBool());
  // The arrows move between the cards too.
  tap(window, Qt::Key_Left);
  CHECK(scene.focusName() == QStringLiteral("room_a"));
  tap(window, Qt::Key_Right);
  CHECK(scene.focusName() == QStringLiteral("room_b"));

  // Enter opens the card: the members, the state, Join under the focus.
  tap(window, Qt::Key_Return);
  CHECK(scene.rooms->selectedId() == QStringLiteral("b"));
  REQUIRE(shown(scene.item("roomDetails")));
  CHECK_FALSE(shown(scene.item("roomList")));
  CHECK(scene.focusName() == QStringLiteral("joinButton"));
  CHECK(scene.item("roomMembers")->property("count").toInt() == 2);
  CHECK(scene.item("roomDetailsState")->property("text").toString() == QStringLiteral("Playing"));

  // Esc closes the card (not the page), back on the room.
  const int depth = scene.navigator->depth();
  tap(window, Qt::Key_Escape);
  CHECK(scene.rooms->selectedId().isEmpty());
  CHECK(scene.navigator->depth() == depth);
  CHECK(scene.focusName() == QStringLiteral("room_b"));

  // Enter, Enter: joined.
  tap(window, Qt::Key_Return);
  tap(window, Qt::Key_Return);
  REQUIRE(waitFor([&] { return scene.count("/SyncPlay/Join") == 1; }, Timeout));
  CHECK(scene.lastBody("/SyncPlay/Join").value(QStringLiteral("GroupId")).toString() == QStringLiteral("b"));
  scene.joined("b", "Cartoons");
  REQUIRE(waitFor([&] { return shown(scene.item("roomPage")); }, Timeout));
  CHECK_FALSE(shown(scene.item("roomsPage")));
  // In the room the list is not asked for.
  REQUIRE(waitFor([&] { return !scene.rooms->active(); }, Timeout));
  CHECK(scene.focusName() == QStringLiteral("leaveButton"));

  // Leave with Enter: the rooms again, polled again.
  tap(window, Qt::Key_Return);
  REQUIRE(waitFor([&] { return scene.count("/SyncPlay/Leave") == 1; }, Timeout));
  scene.left("b");
  REQUIRE(waitFor([&] { return shown(scene.item("roomsPage")) && scene.rooms->active(); }, Timeout));
  CHECK(scene.page()->hasActiveFocus());

  // Esc on the list leaves the page.
  SpatialNavigation::focusItem(scene.card("a"));
  tap(window, Qt::Key_Escape);
  CHECK(scene.navigator->depth() == depth - 1);
}

TEST_CASE("A poll with unchanged rooms keeps their cards; a change updates its card in place") {
  Scene scene;
  REQUIRE(waitFor([&] { return shown(scene.card("a")) && shown(scene.card("b")); }, Timeout));
  QQuickItem* a = scene.card("a");
  QQuickItem* b = scene.card("b");
  const int before = scene.count("/SyncPlay/List");
  REQUIRE(waitFor([&] { return scene.count("/SyncPlay/List") >= before + 3; }, Timeout));
  CHECK(scene.card("a") == a);
  CHECK(scene.card("b") == b);

  scene.list = R"([
    {"GroupId":"a","GroupName":"Movie night","State":"Idle","Participants":["alice"]},
    {"GroupId":"b","GroupName":"Cartoons","State":"Paused","Participants":["bob"]},
    {"GroupId":"c","GroupName":"Concert","State":"Waiting","Participants":["carol"]}])";
  REQUIRE(waitFor([&] { return shown(scene.card("c")); }, Timeout));
  CHECK(scene.card("a") == a);
  CHECK(scene.card("b") == b);
  CHECK(b->property("memberText").toString() == QStringLiteral("bob"));
  CHECK(b->property("stateText").toString() == QStringLiteral("Paused"));
}

TEST_CASE("Empty, offline and the user's SyncPlayAccess on the rooms tab") {
  Scene scene;
  scene.list = "[]";
  REQUIRE(waitFor([&] { return shown(scene.item("roomsEmpty")); }, Timeout));
  CHECK(shown(scene.item("createButton")));

  // No connection: says so, and stops asking.
  scene.rooms->setOnline(false);
  REQUIRE(waitFor([&] { return shown(scene.item("roomsOffline")); }, Timeout));
  CHECK_FALSE(shown(scene.item("roomsEmpty")));
  CHECK_FALSE(shown(scene.item("createButton")));
  pump(150);
  const int listed = scene.count("/SyncPlay/List");
  pump(500);
  CHECK(scene.count("/SyncPlay/List") == listed);
  scene.rooms->setOnline(true);
  REQUIRE(waitFor([&] { return scene.count("/SyncPlay/List") > listed; }, Timeout));
  CHECK_FALSE(shown(scene.item("roomsOffline")));

  // JoinGroups: the rooms and Join, no Create.
  scene.list = TwoRooms;
  scene.access = QStringLiteral("JoinGroups");
  scene.group->checkAccess();
  REQUIRE(waitFor([&] { return !scene.rooms->canCreate(); }, Timeout));
  REQUIRE(waitFor([&] { return shown(scene.card("a")); }, Timeout));
  CHECK_FALSE(shown(scene.item("createButton")));
  CHECK_FALSE(shown(scene.item("roomNameField")));
  QMetaObject::invokeMethod(scene.card("a"), "clicked");
  REQUIRE(shown(scene.item("joinButton")));
  CHECK(scene.item("joinButton")->isEnabled());
  scene.rooms->select(QString());

  // None: nothing to list or join (the app also closes the route).
  scene.access = QStringLiteral("None");
  scene.group->checkAccess();
  REQUIRE(waitFor([&] { return !scene.rooms->available(); }, Timeout));
  REQUIRE(waitFor([&] { return shown(scene.item("roomsUnavailable")); }, Timeout));
  CHECK_FALSE(shown(scene.item("roomList")));
  CHECK_FALSE(shown(scene.item("refreshButton")));
  pump(150);
  const int closed = scene.count("/SyncPlay/List");
  pump(500);
  CHECK(scene.count("/SyncPlay/List") == closed);

  // A refusal from the server shows in words on the tab.
  scene.access = QStringLiteral("CreateAndJoinGroups");
  scene.group->checkAccess();
  REQUIRE(waitFor([&] { return shown(scene.card("a")); }, Timeout));
  scene.rooms->join(QStringLiteral("a"));
  REQUIRE(waitFor([&] { return scene.count("/SyncPlay/Join") == 1; }, Timeout));
  Q_EMIT scene.socket.syncPlayGroupUpdate(json(R"({"GroupId":"a","Type":"LibraryAccessDenied","Data":""})"));
  REQUIRE(waitFor([&] { return shown(scene.item("errorText")); }, Timeout));
  CHECK(scene.item("errorText")->property("text").toString() ==
        session::GroupModel::refusalText(QStringLiteral("LibraryAccessDenied")));
}

TEST_CASE("The room card reads well and shows the focus ring in the dark and the light theme") {
  Scene scene;
  REQUIRE(waitFor([&] { return shown(scene.card("a")) && shown(scene.card("b")); }, Timeout));
  struct Look {
    const char* name;
    QVariantMap palette;
  };
  const Look looks[] = {{"depth", ui::depthPalette()}, {"dark", ui::darkPalette()}, {"light", ui::lightPalette()}};
  for (const Look& look : looks) {
    CAPTURE(std::string(look.name));
    scene.theme->setProperty("colors", look.palette);
    pump(100);
    for (const char* id : {"a", "b"}) {
      QQuickItem* card = scene.card(id);
      QQuickItem* background = find(card, QStringLiteral("roomCardBackground"));
      REQUIRE(background != nullptr);
      for (const bool selected : {false, true}) {
        CAPTURE(selected);
        scene.rooms->select(selected ? QString::fromLatin1(id) : QString());
        pump(50);
        const QColor fill = background->property("color").value<QColor>();
        for (const char* part : {"roomCardTitle", "roomCardState", "roomCardMembers", "roomCardCount"}) {
          CAPTURE(std::string(part));
          CHECK(contrast(find(card, QString::fromLatin1(part)), fill) >= 4.5);
        }
        if (selected) {
          // The open room's line: 3:1 against the card, as a control's edge.
          CHECK(ui::contrastRatio(background->property("border").value<QObject*>()->property("color").value<QColor>(),
                                  fill) >= 3.0);
        }
      }
      scene.rooms->select(QString());
      scene.rooms->select(QString());
      // The ring by keyboard focus: shown, and 3:1 against the page.
      card->forceActiveFocus(Qt::TabFocusReason);
      pump(50);
      REQUIRE(card->property("visualFocus").toBool());
      QQuickItem* ring = nullptr;
      const QList<QQuickItem*> children = background->childItems();
      for (QQuickItem* child : children)
        if (child->property("shown").toBool()) ring = child;
      REQUIRE(ring != nullptr);
      CHECK(ring->isVisible());
      const QColor page = scene.theme->property("background").value<QColor>();
      CHECK(ui::contrastRatio(ring->property("border").value<QObject*>()->property("color").value<QColor>(), page) >= 3.0);
    }
  }
  scene.theme->setProperty("colors", ui::depthPalette());

  // The gallery for the App artifact: both themes, four window sizes, the
  // list and an open card.
  const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
  if (dir.isEmpty()) return;
  QDir().mkpath(dir);
  const QSize sizes[] = {{640, 420}, {1024, 768}, {1280, 720}, {1920, 1080}};
  for (const Look& look : {looks[1], looks[2]}) {
    scene.theme->setProperty("colors", look.palette);
    for (const QSize& size : sizes) {
      scene.window.resize(size);
      scene.shell->setSize(QSizeF(size));
      for (const bool open : {false, true}) {
        scene.rooms->select(open ? QStringLiteral("b") : QString());
        pump(300);
        const QString file = QStringLiteral("%1/rooms-%2%3-%4x%5.png")
                                 .arg(dir, QString::fromLatin1(look.name), open ? QStringLiteral("-card") : QString())
                                 .arg(size.width())
                                 .arg(size.height());
        CHECK(scene.window.grabWindow().save(file));
      }
    }
  }
  scene.rooms->select(QString());
  scene.theme->setProperty("colors", ui::depthPalette());
}
