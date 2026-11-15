#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/LiveTvGuide.h>
#include <jfd/library/LiveTvOnNow.h>
#include <jfd/library/LiveTvSchedule.h>
#include <jfd/platform/InputAction.h>
#include <jfd/ui/InputRouter.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

// The Live TV screens in an offscreen window on a fake server: the guide
// grid driven by keys and by gamepad actions (InputRouter), making only the
// rows and programmes in view; a channel's page; and the home screen's
// "On now" cards, which tune in.

using namespace jfd;
using jfd::platform::InputAction;
using jfd::test::waitFor;

namespace {
  constexpr int Channels = 60;

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  int countNamed(QQuickItem* root, const char* name) {
    int n = root->objectName() == QLatin1String(name) && root->isVisible() ? 1 : 0;
    for (QQuickItem* child : root->childItems()) n += countNamed(child, name);
    return n;
  }

  QString stamp(const QDateTime& at) { return at.toUTC().toString(Qt::ISODate); }

  // Half-hour programmes on every channel from an hour ago to a day ahead,
  // on the half hours of the real clock (the pages use it).
  test::HttpResponse stand(const test::HttpRequest& r) {
    const auto json = [](const QByteArray& body) { return test::HttpResponse{200, body, "application/json", 0}; };
    const auto channel = [](int n) {
      return "{\"Id\": \"c" + QByteArray::number(n) + "\", \"Name\": \"Test Channel " + QByteArray::number(n) +
             "\", \"ChannelNumber\": \"" + QByteArray::number(n) + "\"";
    };
    const qint64 slot = 1800;
    const QDateTime base =
        QDateTime::fromMSecsSinceEpoch((QDateTime::currentSecsSinceEpoch() / slot - 2) * slot * 1000).toUTC();
    const auto program = [&](int n, int i) {
      const QDateTime start = base.addSecs(i * slot);
      return "{\"Id\": \"c" + QByteArray::number(n) + "-" + QByteArray::number(i) + "\", \"ChannelId\": \"c" +
             QByteArray::number(n) + "\", \"Name\": \"Show " + QByteArray::number(n) + "." + QByteArray::number(i) +
             "\", \"StartDate\": \"" + stamp(start).toUtf8() + "\", \"EndDate\": \"" +
             stamp(start.addSecs(slot)).toUtf8() + "\"}";
    };
    const int slots = 52;
    if (r.path == QStringLiteral("/LiveTv/Channels")) {
      const int limit = r.query.queryItemValue(QStringLiteral("limit")).toInt();
      const bool current = r.query.queryItemValue(QStringLiteral("addCurrentProgram")) == QStringLiteral("true");
      QByteArray items;
      for (int n = 1; n <= std::min(Channels, limit); ++n) {
        items += (items.isEmpty() ? "" : ",") + channel(n);
        // "Now" is the third slot.
        if (current) items += ", \"CurrentProgram\": " + program(n, 2);
        items += "}";
      }
      return json("{\"Items\": [" + items + "], \"TotalRecordCount\": " + QByteArray::number(Channels) +
                  ", \"StartIndex\": 0}");
    }
    if (r.path.startsWith(QStringLiteral("/LiveTv/Channels/c"))) {
      const int n = r.path.mid(QStringLiteral("/LiveTv/Channels/c").size()).toInt();
      return json(channel(n) + "}");
    }
    if (r.path == QStringLiteral("/LiveTv/Programs")) {
      const QDateTime minEnd = QDateTime::fromString(r.query.queryItemValue(QStringLiteral("minEndDate")), Qt::ISODateWithMs);
      const QDateTime maxStart = QDateTime::fromString(r.query.queryItemValue(QStringLiteral("maxStartDate")), Qt::ISODateWithMs);
      const QString only = r.query.queryItemValue(QStringLiteral("channelIds"));
      QList<QByteArray> all;
      for (int i = 0; i < slots; ++i) {
        const QDateTime start = base.addSecs(i * slot);
        if (start.addSecs(slot) <= minEnd || start >= maxStart) continue;
        for (int n = 1; n <= Channels; ++n) {
          if (!only.isEmpty() && only != QStringLiteral("c%1").arg(n)) continue;
          all.append(program(n, i));
        }
      }
      const int from = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
      const int limit = std::max(1, r.query.queryItemValue(QStringLiteral("limit")).toInt());
      QByteArray items;
      for (int i = from; i < std::min(int(all.size()), from + limit); ++i)
        items += (items.isEmpty() ? "" : ",") + all[i];
      return json("{\"Items\": [" + items + "], \"TotalRecordCount\": " + QByteArray::number(all.size()) +
                  ", \"StartIndex\": " + QByteArray::number(from) + "}");
    }
    return test::HttpResponse{404, {}, {}, 0};
  }

  struct Scene {
    test::FakeServer server{stand};
    api::JellyfinClient client{api::ClientIdentity{}};
    QQmlEngine engine;
    QQuickWindow window;
    ui::Navigator* navigator = nullptr;
    ui::LibraryUi* library = nullptr;
    std::unique_ptr<QObject> object;
    QQuickItem* root = nullptr;
    QStringList tuned;

    Scene() {
      client.setServerUrl(server.url());
      client.setSession(QStringLiteral("token"), QStringLiteral("user"));
      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      REQUIRE(navigator != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      navigator->reset(QStringLiteral("home"));
      // As LibraryScreens registers it.
      ui::RouteSpec channel;
      channel.name = QStringLiteral("channel");
      channel.takesArgument = true;
      navigator->registerRoute(channel);
      library = engine.singletonInstance<ui::LibraryUi*>("Jfd.Ui.Library", "Library");
      REQUIRE(library != nullptr);
      ui::LibraryUi::Services services;
      services.client = &client;
      library->attach(services);
      library->setChannelHandler([this](const QString& id) { tuned.append(id); });
      window.resize(1280, 800);
    }

    ~Scene() { library->attach({}); }

    void load(const QByteArray& qml) {
      QQmlComponent component(&engine);
      component.setData(qml, QUrl(QStringLiteral("qrc:/livetv-test.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      root = qobject_cast<QQuickItem*>(object.get());
      REQUIRE(root != nullptr);
      root->setParentItem(window.contentItem());
      root->setSize(QSizeF(1280, 800));
      window.show();
    }

    void key(int code) {
      QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
      QCoreApplication::sendEvent(&window, &press);
      QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
      QCoreApplication::sendEvent(&window, &release);
    }

    void screenshot(const char* name) {
      if (const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS"); !dir.isEmpty()) {
        waitFor([] { return false; }, 300);
        window.grabWindow().save(dir + QLatin1Char('/') + QLatin1String(name) + QStringLiteral(".png"));
      }
    }
  };
} // namespace

TEST_CASE("The guide: on screen within a second, only what is in view, keys and a gamepad move the focus") {
  Scene s;
  QElapsedTimer clock;
  clock.start();
  s.load(R"(
import QtQuick
import Jfd.Ui.Library
GuidePage { routeName: "guide"; route: "guide" }
)");
  auto* guide = s.root->property("guide").value<library::LiveTvGuide*>();
  REQUIRE(guide != nullptr);
  s.root->forceActiveFocus();
  REQUIRE(waitFor([&] { return guide->count() == Channels && !guide->loading(); }, 5000));
  REQUIRE(waitFor([&] { return countNamed(s.root, "guideProgram") > 0; }, 2000));
  MESSAGE("guide on screen after " << clock.elapsed() << " ms");
  CHECK(clock.elapsed() < 1000);

  // Virtualised both ways: a few rows of 60, a few hours of 24.
  const int rowsMade = countNamed(s.root, "guideChannel");
  const int cellsMade = countNamed(s.root, "guideProgram");
  MESSAGE(rowsMade << " rows and " << cellsMade << " programmes made");
  CHECK(rowsMade < 20);
  // About the view's hours and one either side, per row.
  CHECK(cellsMade < rowsMade * 16);
  CHECK(guide->viewMinutes() > 60);

  // The focus is on now, first channel; its title in the details.
  CHECK(guide->focusRow() == 0);
  QQuickItem* title = find(s.root, "guideDetailsTitle");
  REQUIRE(title != nullptr);
  CHECK(title->property("text").toString() == QStringLiteral("Show 1.2"));
  s.screenshot("livetv-guide");

  // Keys.
  s.key(Qt::Key_Down);
  CHECK(guide->focusRow() == 1);
  s.key(Qt::Key_Right);
  CHECK(guide->focusedProgram().value(QStringLiteral("title")).toString() == QStringLiteral("Show 2.3"));
  // A gamepad: its actions become the same keys.
  ui::InputRouter router;
  router.setWindow(&s.window);
  router.route(InputAction::Down, false);
  CHECK(guide->focusRow() == 2);
  router.route(InputAction::Left, false);
  CHECK(title->property("text").toString() == QStringLiteral("Show 3.2"));
  // Select on what is on now tunes in.
  router.route(InputAction::Select, false);
  REQUIRE(s.tuned.size() == 1);
  CHECK(s.tuned.front() == QStringLiteral("c3"));
  // Later programmes: Enter opens the channel's page.
  router.route(InputAction::Right, false);
  s.key(Qt::Key_Return);
  CHECK(s.navigator->currentRoute() == QStringLiteral("channel/c3"));
  CHECK(s.tuned.size() == 1);
  s.navigator->back();

  // LB / RB (Page Up / Down) move a screen of time; the view follows.
  const int before = guide->focusMinutes();
  router.route(InputAction::PageRight, false);
  CHECK(guide->focusMinutes() == before + guide->viewMinutes());
  CHECK(guide->viewStartMinutes() > 0);
  REQUIRE(waitFor([&] { return !guide->loading(); }, 3000));
  CHECK(countNamed(s.root, "guideProgram") < rowsMade * 16);
  router.route(InputAction::PageLeft, false);
  s.key(Qt::Key_Home);
  CHECK(guide->viewStartMinutes() == 0);

  // Far down the list: the list scrolls to the focused row.
  for (int i = 0; i < 40; ++i) s.key(Qt::Key_Down);
  CHECK(guide->focusRow() == 42);
  REQUIRE(waitFor([&] {
    QQuickItem* rows = find(s.root, "guideRows");
    return rows && rows->property("contentY").toDouble() > 0;
  }));
  CHECK(countNamed(s.root, "guideChannel") < 20);
}

TEST_CASE("A channel's page: Watch has the focus and tunes in; its programmes list from now") {
  Scene s;
  s.load(R"(
import QtQuick
import Jfd.Ui.Library
ChannelPage { routeName: "channel"; routeArgument: "c7"; route: "channel/c7" }
)");
  auto* schedule = s.root->property("schedule").value<library::LiveTvSchedule*>();
  REQUIRE(schedule != nullptr);
  REQUIRE(waitFor([&] { return !schedule->loading() && schedule->count() > 0; }, 5000));
  CHECK(schedule->name() == QStringLiteral("Test Channel 7"));
  QQuickItem* name = find(s.root, "channelName");
  REQUIRE(name != nullptr);
  CHECK(name->property("text").toString() == QStringLiteral("7  Test Channel 7"));
  CHECK(schedule->data(schedule->index(0), library::LiveTvSchedule::OnNowRole).toBool());
  QQuickItem* watch = find(s.root, "channelWatch");
  REQUIRE(watch != nullptr);
  s.root->forceActiveFocus();
  REQUIRE(waitFor([&] { return watch->hasActiveFocus(); }, 2000));
  s.screenshot("livetv-channel");
  s.key(Qt::Key_Return);
  REQUIRE(s.tuned.size() == 1);
  CHECK(s.tuned.front() == QStringLiteral("c7"));
}

TEST_CASE("On now on the home screen: cards with the programme and channel; a card tunes in") {
  Scene s;
  library::LiveTvOnNow onNow(s.client);
  onNow.reload();
  REQUIRE(waitFor([&] { return onNow.count() == 24; }, 5000));
  s.engine.rootContext()->setContextProperty(QStringLiteral("onNowModel"), &onNow);
  s.load(R"(
import QtQuick
import Jfd.Ui.Controls
import Jfd.Ui.Library
Item {
  CardRow {
    objectName: "onNow"
    width: parent.width
    title: "On now"
    model: onNowModel
    delegate: ItemCard { shape: "thumb" }
  }
}
)");
  QQuickItem* row = find(s.root, "onNow");
  REQUIRE(row != nullptr);
  REQUIRE(waitFor([&] { return row->property("count").toInt() == 24; }));
  auto* list = row->property("view").value<QQuickItem*>();
  REQUIRE(list != nullptr);
  QQuickItem* card = nullptr;
  REQUIRE(waitFor([&] {
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, card), Q_ARG(int, 4));
    return card != nullptr;
  }));
  CHECK(card->property("title").toString() == QStringLiteral("Show 5.2"));
  CHECK(card->property("subtitle").toString() == QStringLiteral("5 · Test Channel 5"));
  CHECK(card->property("progress").toDouble() > 0.0);
  s.screenshot("livetv-onnow");
  QMetaObject::invokeMethod(card, "clicked");
  REQUIRE(s.tuned.size() == 1);
  CHECK(s.tuned.front() == QStringLiteral("c5"));
}
