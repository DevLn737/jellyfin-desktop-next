// The sign-in screens in TV mode on a fake server, driven only by arrows,
// Enter (Select) and Back, as with a remote or a gamepad: every control is
// reached and none traps the focus, the server's address and the password
// come from the on-screen keyboard, and Quick Connect comes first.
#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/ServerDiscovery.h>
#include <jfd/platform/CredentialStore.h>
#include <jfd/account/Account.h>
#include <jfd/ui/LibraryScreens.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/SpatialNavigation.h>
#include <jfd/ui/TextEntry.h>

#include <QFont>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

using namespace jfd;
using jfd::account::SignIn;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;
using ui::SpatialNavigation;

namespace {

  constexpr int Timeout = 10000;

  HttpResponse ok(const QByteArray& body) { return {200, body, "application/json", 0}; }

  const QByteArray Info =
      R"({"Id": "srv1", "ServerName": "Home", "Version": "12.2.0", "StartupWizardCompleted": true})";
  const QByteArray Auth = R"({"AccessToken": "tok-new", "ServerId": "srv1",
    "User": {"Id": "u1", "Name": "alice", "ServerId": "srv1"}})";

  // Sign-in and Quick Connect (authorized at the second poll); the rest of
  // the API answers empty, enough for the home page that follows.
  struct Routes {
    int quickConnectPolls = 0;
    QString password = QStringLiteral("secret");
    // Users/Public; empty: answered like the rest (not a list).
    QByteArray publicUsers;

    HttpResponse operator()(const HttpRequest& r) {
      const QString p = r.path;
      if (p == QLatin1String("/System/Info/Public")) return ok(Info);
      if (p == QLatin1String("/QuickConnect/Enabled")) return ok("true");
      if (p == QLatin1String("/Users/Public") && !publicUsers.isEmpty()) return ok(publicUsers);
      if (p == QLatin1String("/Users/AuthenticateByName")) {
        const QJsonObject body = QJsonDocument::fromJson(r.body).object();
        if (body.value(QStringLiteral("Username")).toString() != QLatin1String("alice") ||
            body.value(QStringLiteral("Pw")).toString() != password)
          return {401, {}, {}, 0};
        return ok(Auth);
      }
      if (p == QLatin1String("/QuickConnect/Initiate"))
        return ok(R"({"Secret": "s3cr3t", "Code": "123456", "Authenticated": false})");
      if (p == QLatin1String("/QuickConnect/Connect"))
        return ok(++quickConnectPolls >= 2 ? R"({"Secret": "s3cr3t", "Code": "123456", "Authenticated": true})"
                                           : R"({"Secret": "s3cr3t", "Code": "123456", "Authenticated": false})");
      if (p == QLatin1String("/Users/AuthenticateWithQuickConnect")) return ok(Auth);
      if (p == QLatin1String("/Users/Me")) return ok(R"({"Id": "u1", "Name": "alice", "ServerId": "srv1"})");
      return ok(R"({"Items": [], "TotalRecordCount": 0})");
    }
  };

  // A key as the gamepad or a remote sends it (InputRouter).
  void tap(QQuickWindow& window, int key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &release);
  }

  using Node = QQuickItem*;

  QSet<Node> reach(const std::map<Node, QSet<Node>>& edges, Node start) {
    QSet<Node> seen{start};
    std::deque<Node> queue{start};
    while (!queue.empty()) {
      const Node node = queue.front();
      queue.pop_front();
      const auto it = edges.find(node);
      if (it == edges.end()) continue;
      for (const Node next : it->second) {
        if (seen.contains(next)) continue;
        seen.insert(next);
        queue.push_back(next);
      }
    }
    return seen;
  }

  class Screens {
  public:
    Screens() {
      account::Account::Config accountConfig;
      ui::LibraryScreens::Config config;
      accountConfig.identity.version = QStringLiteral("0.0.1");
      accountConfig.identity.device = QStringLiteral("ui-test");
      accountConfig.identity.deviceId = QStringLiteral("jfd-ui-login-tv");
      accountConfig.serversFile = m_dir.filePath(QStringLiteral("servers.ini"));
      config.imageDirectory = m_dir.filePath(QStringLiteral("images"));
      accountConfig.tokens = std::make_unique<platform::MemoryCredentialStore>();
      userAccount = std::make_unique<account::Account>(std::move(accountConfig));
      screens = std::make_unique<ui::LibraryScreens>(*userAccount, std::move(config));
      userAccount->signIn().setQuickConnectPollMs(20);
      // No broadcast from the test: discovery asks a closed local port.
      userAccount->signIn().discovery().setTargets({{QHostAddress(QHostAddress::LocalHost), quint16(9)}});

      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      REQUIRE(navigator != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      REQUIRE(screens->install(engine, *navigator));
      navigator->reset(QStringLiteral("login"));

      engine.loadData(R"(
        import QtQuick
        import Jfd.Ui.Theme
        import Jfd.Ui.Shell
        Window {
            width: 1920
            height: 1080
            visible: true
            Component.onCompleted: Theme.tvMode = true
            AppShell { objectName: "shell"; anchors.fill: parent }
        })");
      REQUIRE_FALSE(engine.rootObjects().isEmpty());
      window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
      REQUIRE(window != nullptr);
      entry = window->findChild<ui::TextEntry*>(QStringLiteral("textEntry"));
      REQUIRE(entry != nullptr);
      REQUIRE(waitFor([&] { return page() != nullptr && entry->isActive(); }, Timeout));
    }

    QQuickItem* page() const {
      auto* stack = window->findChild<QQuickItem*>(QStringLiteral("pageStack"));
      return stack == nullptr ? nullptr : stack->property("currentItem").value<QQuickItem*>();
    }

    QQuickItem* find(const char* name) const {
      QQuickItem* current = page();
      return current == nullptr ? nullptr : current->findChild<QQuickItem*>(QString::fromLatin1(name));
    }
    // Through the visual tree, for delegates.
    QQuickItem* item(const char* name) const {
      std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* node) -> QQuickItem* {
        if (node == nullptr || node->objectName() == QLatin1String(name)) return node;
        const QList<QQuickItem*> children = node->childItems();
        for (QQuickItem* child : children)
          if (QQuickItem* found = walk(child)) return found;
        return nullptr;
      };
      return walk(page());
    }

    // The TextInput of an InputField.
    QQuickItem* input(const char* name) const {
      QQuickItem* field = find(name);
      return field ? field->property("field").value<QQuickItem*>() : nullptr;
    }

    SignIn::Step step() { return userAccount->signIn().step(); }

    bool waitStep(SignIn::Step wanted) {
      return waitFor([&] { return step() == wanted; }, Timeout);
    }

    QString focusName() const {
      const QQuickItem* item = window->activeFocusItem();
      if (!item) return {};
      // A field's TextInput is named by its InputField.
      if (!item->objectName().isEmpty()) return item->objectName();
      for (const QQuickItem* node = item->parentItem(); node; node = node->parentItem())
        if (!node->objectName().isEmpty() && node->inherits("QQuickFocusScope")) return node->objectName();
      return {};
    }

    // The visible, enabled items on the page that take the focus by keys.
    QList<QQuickItem*> focusable() const {
      QList<QQuickItem*> found;
      std::function<void(QQuickItem*)> walk = [&](QQuickItem* node) {
        if (!node->isVisible() || !node->isEnabled()) return;
        if (node->activeFocusOnTab() && node->width() > 0 && node->height() > 0) found.append(node);
        const QList<QQuickItem*> children = node->childItems();
        for (QQuickItem* child : children) walk(child);
      };
      walk(page());
      return found;
    }

    // The innermost of `items` that holds the focus (a field's InputField
    // and its TextInput are one).
    Node nodeOf(const QList<QQuickItem*>& items) const {
      QQuickItem* focus = window->activeFocusItem();
      Node best = nullptr;
      for (QQuickItem* item : items)
        if ((item == focus || item->isAncestorOf(focus)) && (!best || best->isAncestorOf(item))) best = item;
      return best;
    }

    // The arrows over every focusable item of the page: each reaches each
    // other, the on-screen keyboard that a field opens closes with Back on
    // the field, and the safe area holds them all.
    void checkNoTraps() {
      const QRectF safe(96, 54, 1920 - 2 * 96, 1080 - 2 * 54);
      const auto sceneRect = [](const QQuickItem* item) {
        return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
      };
      // A step that has just loaded is laid out at the next polish (the
      // Column places its children then): wait for it before measuring.
      waitFor([&] {
        const QList<QQuickItem*> now = focusable();
        return std::all_of(now.begin(), now.end(), [&](const QQuickItem* item) { return safe.contains(sceneRect(item)); });
      }, 2000);
      const QList<QQuickItem*> items = focusable();
      QSet<Node> nodes;
      for (QQuickItem* item : items) {
        CAPTURE(item->objectName());
        CAPTURE(QString::fromLatin1(item->metaObject()->className()));
        const QRectF rect = sceneRect(item);
        CAPTURE(QStringLiteral("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
        CHECK(safe.contains(rect));
        SpatialNavigation::focusItem(item);
        if (Node node = nodeOf(items)) nodes.insert(node);
      }
      REQUIRE(nodes.size() >= 3);
      std::map<Node, QSet<Node>> edges;
      std::map<Node, QSet<Node>> reverse;
      for (const Node item : std::as_const(nodes)) {
        for (const int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
          SpatialNavigation::focusItem(item);
          REQUIRE(nodeOf(items) == item);
          REQUIRE_FALSE(entry->isOpen());
          tap(*window, key);
          const Node to = nodeOf(items);
          if (entry->isOpen()) {
            // On a field: Back closes the keyboard and stays there.
            tap(*window, Qt::Key_Escape);
            CHECK_FALSE(entry->isOpen());
            CHECK(nodeOf(items) == to);
          }
          if (to && to != item && nodes.contains(to)) {
            edges[item].insert(to);
            reverse[to].insert(item);
          }
        }
      }
      const Node start = *nodes.begin();
      const QSet<Node> forward = reach(edges, start);
      const QSet<Node> backward = reach(reverse, start);
      for (const Node node : std::as_const(nodes)) {
        CAPTURE(node->objectName());
        CAPTURE(node->property("text").toString());
        CHECK(forward.contains(node));
        CHECK(backward.contains(node));
      }
    }

    // The highlighted key of the on-screen keyboard.
    QString highlighted() const {
      QQuickItem* board = window->findChild<QQuickItem*>(QStringLiteral("onScreenKeyboard"));
      const QVariantList rows = board->property("rows").toList();
      const QVariantMap key =
          rows.at(board->property("row").toInt()).toList().at(board->property("column").toInt()).toMap();
      return key.contains(QStringLiteral("action")) ? key.value(QStringLiteral("action")).toString()
                                                     : key.value(QStringLiteral("text")).toString();
    }
    int keyboardRow() const {
      return window->findChild<QQuickItem*>(QStringLiteral("onScreenKeyboard"))->property("row").toInt();
    }

    // Arrows to the key (from the bottom row up), then Select.
    void typeKey(const QString& name) {
      REQUIRE(entry->isOpen());
      for (int i = 0; i < 6; ++i) tap(*window, Qt::Key_Down);
      bool found = false;
      while (!found) {
        for (int i = 0; i < 14; ++i) tap(*window, Qt::Key_Left);
        for (int i = 0; i < 14 && !found; ++i) {
          found = highlighted() == name;
          if (!found) tap(*window, Qt::Key_Right);
        }
        if (found) break;
        REQUIRE(keyboardRow() > 0);
        tap(*window, Qt::Key_Up);
      }
      tap(*window, Qt::Key_Return);
    }

    // Letters, digits and the symbols in between, on the keyboard alone.
    void typeText(const QString& text) {
      for (const QChar c : text) {
        if (c.isLetterOrNumber() || c == QLatin1Char('.')) {
          typeKey(QString(c));
        } else {
          typeKey(QStringLiteral("mode"));
          typeKey(QString(c));
          typeKey(QStringLiteral("mode"));
        }
      }
    }

    Routes routes;
    FakeServer server{[this](const HttpRequest& r) { return routes(r); }};
    QTemporaryDir m_dir;
    std::unique_ptr<account::Account> userAccount;
    std::unique_ptr<ui::LibraryScreens> screens;
    QQmlApplicationEngine engine;
    ui::Navigator* navigator = nullptr;
    QQuickWindow* window = nullptr;
    ui::TextEntry* entry = nullptr;
  };

} // namespace

TEST_CASE("TV sign-in with arrows, Select and Back only: the address and the password from the on-screen keyboard") {
  Screens screens;
  QQuickWindow& window = *screens.window;
  REQUIRE(screens.step() == SignIn::Step::ChooseServer);
  REQUIRE(waitFor([&] { return !screens.userAccount->signIn().discovering(); }, Timeout));

  // The server step: the address field has the focus, no keyboard until
  // Select.
  REQUIRE(screens.input("serverAddress") != nullptr);
  CHECK(window.activeFocusItem() == screens.input("serverAddress"));
  CHECK_FALSE(screens.entry->isOpen());

  // The address on the keyboard (Latin letters for an address).
  tap(window, Qt::Key_Return);
  REQUIRE(screens.entry->isOpen());
  CHECK(screens.highlighted() == QStringLiteral("q"));
  const QString address = QStringLiteral("127.0.0.1:%1").arg(screens.server.url().port());
  screens.typeText(address);
  CHECK(screens.input("serverAddress")->property("text").toString() == address);

  // Back closes the keyboard; nothing on the step traps the focus.
  tap(window, Qt::Key_Escape);
  CHECK_FALSE(screens.entry->isOpen());
  CHECK(screens.find("connectButton")->isEnabled());
  screens.checkNoTraps();

  // Select on the field again, then Done: connected.
  SpatialNavigation::focusItem(screens.find("serverAddress"));
  tap(window, Qt::Key_Return);
  REQUIRE(screens.entry->isOpen());
  screens.typeKey(QStringLiteral("done"));
  REQUIRE(screens.waitStep(SignIn::Step::Credentials));
  CHECK_FALSE(screens.entry->isOpen());

  // Quick Connect first: the main button, focused, above the fields.
  REQUIRE(waitFor([&] { return screens.focusName() == QLatin1String("quickConnectTvButton"); }, Timeout));
  QQuickItem* quick = screens.find("quickConnectTvButton");
  CHECK(quick->property("primary").toBool());
  CHECK_FALSE(screens.find("signInButton")->property("primary").toBool());
  CHECK_FALSE(screens.find("quickConnectButton")->isVisible());
  CHECK(quick->mapToScene(QPointF()).y() < screens.find("userName")->mapToScene(QPointF()).y());
  CHECK(quick->height() >= 64);
  screens.checkNoTraps();

  // Back: one step back, to the servers; and on again.
  SpatialNavigation::focusItem(screens.find("quickConnectTvButton"));
  tap(window, Qt::Key_Escape);
  REQUIRE(screens.waitStep(SignIn::Step::ChooseServer));
  CHECK(screens.navigator->currentRoute() == QLatin1String("login"));
  screens.userAccount->signIn().connectToServer(screens.server.url().toString());
  REQUIRE(screens.waitStep(SignIn::Step::Credentials));
  REQUIRE(waitFor([&] { return screens.focusName() == QLatin1String("quickConnectTvButton"); }, Timeout));

  // Down to the name: the keyboard opens; Done goes on to the password with
  // the keyboard still open; Done there signs in.
  tap(window, Qt::Key_Down);
  REQUIRE(window.activeFocusItem() == screens.input("userName"));
  REQUIRE(screens.entry->isOpen());
  screens.typeText(QStringLiteral("alice"));
  screens.typeKey(QStringLiteral("done"));
  REQUIRE(window.activeFocusItem() == screens.input("password"));
  REQUIRE(screens.entry->isOpen());
  screens.typeText(QStringLiteral("secret"));
  screens.typeKey(QStringLiteral("done"));
  REQUIRE(screens.waitStep(SignIn::Step::SignedIn));
  CHECK_FALSE(screens.entry->isOpen());
  REQUIRE(waitFor([&] { return screens.navigator->currentRoute() == QLatin1String("home"); }, Timeout));
}

TEST_CASE("TV sign-in with Quick Connect: Select shows the code, Back cancels it, the code signs in") {
  Screens screens;
  QQuickWindow& window = *screens.window;
  screens.userAccount->signIn().connectToServer(screens.server.url().toString());
  REQUIRE(screens.waitStep(SignIn::Step::Credentials));
  REQUIRE(waitFor([&] { return screens.focusName() == QLatin1String("quickConnectTvButton"); }, Timeout));

  // Select: the code, large; Back: the password form again, Quick Connect
  // focused. Polls do not authorize meanwhile (the fake does at the second).
  screens.routes.quickConnectPolls = -1000;
  tap(window, Qt::Key_Return);
  REQUIRE(screens.waitStep(SignIn::Step::QuickConnect));
  REQUIRE(waitFor([&] { return screens.find("quickConnectCode") != nullptr &&
                               screens.find("quickConnectCode")->property("text").toString() == QLatin1String("123456"); },
                  Timeout));
  CHECK(screens.find("quickConnectCode")->property("font").value<QFont>().pixelSize() >= 44);
  tap(window, Qt::Key_Escape);
  REQUIRE(screens.waitStep(SignIn::Step::Credentials));
  REQUIRE(waitFor([&] { return screens.focusName() == QLatin1String("quickConnectTvButton"); }, Timeout));

  // Again, and the code is entered elsewhere: signed in.
  screens.routes.quickConnectPolls = 0;
  tap(window, Qt::Key_Return);
  REQUIRE(screens.waitStep(SignIn::Step::QuickConnect));
  REQUIRE(screens.waitStep(SignIn::Step::SignedIn));
  REQUIRE(waitFor([&] { return screens.navigator->currentRoute() == QLatin1String("home"); }, Timeout));
}

TEST_CASE("Who's watching: the server's users fill the name; nothing else changes without them") {
  Screens screens;
  QQuickWindow& window = *screens.window;
  screens.routes.publicUsers = R"([{"Id": "u1", "Name": "alice", "HasPassword": true},
                                   {"Id": "u2", "Name": "bob", "HasPassword": false}])";
  screens.userAccount->signIn().connectToServer(screens.server.url().toString());
  REQUIRE(screens.waitStep(SignIn::Step::Credentials));
  REQUIRE(waitFor([&] { return screens.item("loginUser_bob") != nullptr; }, Timeout));
  REQUIRE(screens.item("loginUser_alice") != nullptr);
  CHECK(screens.item("loginUserOther")->isVisible());
  // Every user and the way to another server are reachable by arrows.
  screens.checkNoTraps();

  const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
  if (!dir.isEmpty()) {
    // As on a desktop.
    QObject* theme = screens.engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme");
    REQUIRE(theme != nullptr);
    theme->setProperty("tvMode", false);
    waitFor([] { return false; }, 300);
    CHECK(window.grabWindow().save(dir + QStringLiteral("/login-users.png")));
  }

  // A user: the name, and the password next.
  QMetaObject::invokeMethod(screens.item("loginUser_alice"), "clicked");
  CHECK(screens.input("userName")->property("text").toString() == QStringLiteral("alice"));
  CHECK(window.activeFocusItem() == screens.input("password"));
  CHECK(screens.item("loginUser_alice")->property("selected").toBool());
  // Other: an empty name to type.
  QMetaObject::invokeMethod(screens.item("loginUserOther"), "clicked");
  CHECK(screens.input("userName")->property("text").toString().isEmpty());
  CHECK(window.activeFocusItem() == screens.input("userName"));

  // Change: back to the servers.
  QMetaObject::invokeMethod(screens.find("changeServer"), "clicked");
  REQUIRE(screens.waitStep(SignIn::Step::ChooseServer));
  // A server that hides its users: the plain form.
  screens.routes.publicUsers = "[]";
  screens.userAccount->signIn().connectToServer(screens.server.url().toString());
  REQUIRE(screens.waitStep(SignIn::Step::Credentials));
  waitFor([] { return false; }, 200);
  CHECK(screens.userAccount->signIn().publicUsers().isEmpty());
  CHECK_FALSE(screens.find("loginUsers")->isVisible());
}
