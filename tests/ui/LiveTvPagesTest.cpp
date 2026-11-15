#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/LiveTvGuide.h>
#include <jfd/library/LiveTvOnNow.h>
#include <jfd/library/LiveTvRecorder.h>
#include <jfd/library/LiveTvRecordings.h>
#include <jfd/library/LiveTvSchedule.h>
#include <jfd/platform/InputAction.h>
#include <jfd/ui/InputRouter.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <memory>
#include <optional>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

// The Live TV screens in an offscreen window on a fake server: the guide
// grid driven by keys and by gamepad actions (InputRouter), making only the
// rows and programmes in view; a channel's page; the home screen's "On now"
// cards, which tune in; and recording: the actions on the guide and a
// channel's page, the marks, and the Recordings screen.

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
             stamp(start.addSecs(slot)).toUtf8() + "\"" + (n == 2 ? ", \"IsSeries\": true" : "") + "}";
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

  // The server's DVR for the recording tests: timers by programme, series
  // timers that schedule the series' next programmes, recordings.
  struct Dvr {
    struct Timer {
      QString id;
      QString programId;
      QString seriesId;
      QString status = QStringLiteral("New");
    };
    bool manage = true;
    QList<Timer> timers;
    QStringList series;
    QList<QByteArray> recordings;
    QStringList calls;

    std::optional<test::HttpResponse> answer(const test::HttpRequest& r) {
      const auto json = [](const QByteArray& body) { return test::HttpResponse{200, body, "application/json", 0}; };
      const test::HttpResponse none{204, {}, {}, 0};
      if (r.path == QStringLiteral("/LiveTv/Info"))
        return json(R"({"Services": [{"Name": "Emby", "Status": "Ok"}], "IsEnabled": true, "EnabledUsers": []})");
      if (r.path == QStringLiteral("/Users/Me"))
        return json(QByteArray(R"({"Id": "user", "Name": "alice", "Policy": {"IsAdministrator": false, "EnableLiveTvManagement": )") +
                    (manage ? "true" : "false") + "}}");
      if (r.path == QStringLiteral("/LiveTv/Timers") && r.method == "GET") {
        QByteArray items;
        for (const Timer& t : timers)
          items += (items.isEmpty() ? "" : ",") + QByteArray(R"({"Id": ")") + t.id.toUtf8() + R"(", "Type": "Timer", "ProgramId": ")" +
                   t.programId.toUtf8() + R"(", "SeriesTimerId": ")" + t.seriesId.toUtf8() + R"(", "Status": ")" +
                   t.status.toUtf8() + R"(", "Name": "Show )" + t.programId.toUtf8() + R"(", "ChannelName": "Test Channel"})";
        return json("{\"Items\": [" + items + "], \"TotalRecordCount\": " + QByteArray::number(timers.size()) + "}");
      }
      if (r.path == QStringLiteral("/LiveTv/SeriesTimers") && r.method == "GET") {
        QByteArray items;
        for (const QString& id : series)
          items += (items.isEmpty() ? "" : ",") + QByteArray(R"({"Id": ")") + id.toUtf8() +
                   R"(", "Type": "SeriesTimer", "Name": "Series )" + id.toUtf8() + R"(", "ChannelName": "Test Channel 2"})";
        return json("{\"Items\": [" + items + "], \"TotalRecordCount\": " + QByteArray::number(series.size()) + "}");
      }
      if (r.path == QStringLiteral("/LiveTv/Timers/Defaults"))
        return json(R"({"Type": "SeriesTimer", "ProgramId": ")" + r.query.queryItemValue(QStringLiteral("programId")).toUtf8() +
                    R"(", "RecordNewOnly": true})");
      const QString programId = QJsonDocument::fromJson(r.body).object().value(QStringLiteral("ProgramId")).toString();
      if (r.path == QStringLiteral("/LiveTv/Timers") && r.method == "POST") {
        calls.append(QStringLiteral("record:") + programId);
        timers.append({QStringLiteral("t-") + programId, programId, {}});
        return none;
      }
      if (r.path == QStringLiteral("/LiveTv/SeriesTimers") && r.method == "POST") {
        calls.append(QStringLiteral("series:") + programId);
        series.append(QStringLiteral("s1"));
        // This programme and the next two of its channel ("c2-3").
        const QString prefix = programId.section(QLatin1Char('-'), 0, 0);
        const int at = programId.section(QLatin1Char('-'), 1, 1).toInt();
        for (int i = at; i < at + 3; ++i) {
          const QString id = QStringLiteral("%1-%2").arg(prefix).arg(i);
          timers.append({QStringLiteral("t-") + id, id, QStringLiteral("s1")});
        }
        return none;
      }
      if (r.method == "DELETE" && r.path.startsWith(QStringLiteral("/LiveTv/Timers/"))) {
        const QString id = r.path.section(QLatin1Char('/'), -1);
        calls.append(QStringLiteral("cancel:") + id);
        timers.erase(std::remove_if(timers.begin(), timers.end(), [&](const Timer& t) { return t.id == id; }), timers.end());
        return none;
      }
      if (r.method == "DELETE" && r.path.startsWith(QStringLiteral("/LiveTv/SeriesTimers/"))) {
        const QString id = r.path.section(QLatin1Char('/'), -1);
        calls.append(QStringLiteral("cancelSeries:") + id);
        series.removeAll(id);
        timers.erase(std::remove_if(timers.begin(), timers.end(), [&](const Timer& t) { return t.seriesId == id; }), timers.end());
        return none;
      }
      if (r.path == QStringLiteral("/LiveTv/Recordings")) {
        QByteArray items;
        for (const QByteArray& one : recordings) items += (items.isEmpty() ? "" : ",") + one;
        return json("{\"Items\": [" + items + "], \"TotalRecordCount\": " + QByteArray::number(recordings.size()) + "}");
      }
      if (r.method == "DELETE" && r.path.startsWith(QStringLiteral("/Items/"))) {
        const QString id = r.path.section(QLatin1Char('/'), -1);
        calls.append(QStringLiteral("delete:") + id);
        recordings.erase(std::remove_if(recordings.begin(), recordings.end(),
                                        [&](const QByteArray& one) { return one.contains("\"Id\": \"" + id.toUtf8() + "\""); }),
                         recordings.end());
        return none;
      }
      return std::nullopt;
    }
  };

  struct Scene {
    Dvr dvr;
    test::FakeServer server{[this](const test::HttpRequest& r) {
      if (auto answer = dvr.answer(r)) return *answer;
      return stand(r);
    }};
    api::JellyfinClient client{api::ClientIdentity{}};
    library::LiveTvRecorder recorder{client};
    QQmlEngine engine;
    QQuickWindow window;
    ui::Navigator* navigator = nullptr;
    ui::LibraryUi* library = nullptr;
    std::unique_ptr<QObject> object;
    QQuickItem* root = nullptr;
    QStringList tuned;
    QStringList played;
    qint64 playedAt = -1;

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
      ui::RouteSpec recordings;
      recordings.name = QStringLiteral("recordings");
      navigator->registerRoute(recordings);
      ui::LibraryUi::Services services;
      services.client = &client;
      services.recorder = &recorder;
      library->attach(services);
      library->setChannelHandler([this](const QString& id) { tuned.append(id); });
      library->setPlayHandler([this](const QString& id, qint64 ticks) {
        played.append(id);
        playedAt = ticks;
      });
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

    // Recording allowed (or not) and the schedule read.
    void checkRecorder() {
      recorder.checkAvailable();
      if (dvr.manage) {
        REQUIRE(waitFor([&] { return recorder.available() && !recorder.loading(); }, 5000));
      } else {
        REQUIRE(waitFor([&] {
          return std::any_of(server.requests().cbegin(), server.requests().cend(),
                             [](const test::HttpRequest& r) { return r.path == QStringLiteral("/Users/Me"); });
        }, 5000));
        waitFor([] { return false; }, 100);
        REQUIRE_FALSE(recorder.available());
      }
    }

    // Visible items named `name` anywhere in the window, popups included.
    int visibleInWindow(const char* name) { return countNamed(window.contentItem(), name); }
    QList<QQuickItem*> allNamed(QQuickItem* from, const char* name) {
      QList<QQuickItem*> out;
      if (from->objectName() == QLatin1String(name) && from->isVisible()) out.append(from);
      for (QQuickItem* child : from->childItems()) out += allNamed(child, name);
      return out;
    }
    QString focusedName() const {
      return window.activeFocusItem() ? window.activeFocusItem()->objectName() : QString();
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

TEST_CASE("Recording from the guide: the Menu key's actions, R, the marks; a gamepad's View button") {
  Scene s;
  s.checkRecorder();
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
  CHECK(countNamed(s.root, "guideRecordingMark") == 0);
  QQuickItem* actions = find(s.root, "guideRecordActions");
  REQUIRE(actions != nullptr);
  CHECK(actions->isVisible());
  CHECK(countNamed(s.root, "guideRecordings") == 1);

  // Menu on what is on now (channel 1): Record only, it is not a series.
  REQUIRE(guide->focusedProgram().value(QStringLiteral("id")).toString() == QStringLiteral("c1-2"));
  s.key(Qt::Key_Menu);
  REQUIRE(waitFor([&] { return s.visibleInWindow("popupEntry") == 1; }));
  REQUIRE(waitFor([&] { return s.focusedName() == QStringLiteral("popupEntry"); }));
  CHECK(s.window.activeFocusItem()->property("text").toString() == QStringLiteral("Record"));
  s.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return countNamed(s.root, "guideRecordingMark") == 1; }, 3000));
  CHECK(s.dvr.calls == QStringList{QStringLiteral("record:c1-2")});
  CHECK(guide->focusedProgram().value(QStringLiteral("timerId")).toString() == QStringLiteral("t-c1-2"));
  // The focus came back to the guide: its keys work.
  REQUIRE(waitFor([&] { return s.visibleInWindow("popupEntry") == 0; }));
  s.screenshot("livetv-guide-recording");

  // R cancels it.
  s.key(Qt::Key_R);
  REQUIRE(waitFor([&] { return countNamed(s.root, "guideRecordingMark") == 0; }, 3000));
  CHECK(s.dvr.calls.last() == QStringLiteral("cancel:t-c1-2"));

  // Channel 2 shows a series: Record and Record series, by a gamepad.
  s.key(Qt::Key_Down);
  REQUIRE(guide->focusedProgram().value(QStringLiteral("id")).toString() == QStringLiteral("c2-2"));
  ui::InputRouter router;
  router.setWindow(&s.window);
  router.route(InputAction::Menu, false);
  REQUIRE(waitFor([&] { return s.visibleInWindow("popupEntry") == 2; }));
  REQUIRE(waitFor([&] { return s.focusedName() == QStringLiteral("popupEntry"); }));
  router.route(InputAction::Down, false);
  CHECK(s.window.activeFocusItem()->property("text").toString() == QStringLiteral("Record series"));
  router.route(InputAction::Select, false);
  // The series' programmes in view are marked: now and the next two.
  REQUIRE(waitFor([&] { return countNamed(s.root, "guideRecordingMark") == 3; }, 3000));
  CHECK(s.dvr.calls.last() == QStringLiteral("series:c2-2"));
  CHECK(guide->focusedProgram().value(QStringLiteral("seriesTimerId")).toString() == QStringLiteral("s1"));

  // Back closes the menu without doing anything; the guide keeps its keys.
  router.route(InputAction::Menu, false);
  REQUIRE(waitFor([&] { return s.visibleInWindow("popupEntry") == 2; }));
  router.route(InputAction::Back, false);
  REQUIRE(waitFor([&] { return s.visibleInWindow("popupEntry") == 0; }));
  const int calls = int(s.dvr.calls.size());
  s.key(Qt::Key_Down);
  CHECK(guide->focusRow() == 2);
  CHECK(s.dvr.calls.size() == calls);

  // The actions' buttons do the same for the mouse.
  s.key(Qt::Key_Up);
  QList<QQuickItem*> buttons = s.allNamed(s.root, "guideRecordAction");
  REQUIRE(buttons.size() == 2);
  CHECK(buttons[0]->property("text").toString() == QStringLiteral("Cancel recording"));
  CHECK(buttons[1]->property("text").toString() == QStringLiteral("Cancel series recording"));
  QMetaObject::invokeMethod(buttons[1], "clicked");
  REQUIRE(waitFor([&] { return countNamed(s.root, "guideRecordingMark") == 0; }, 3000));
  CHECK(s.dvr.calls.last() == QStringLiteral("cancelSeries:s1"));
}

TEST_CASE("Without the right to record, the guide and a channel's page offer no recording") {
  Scene s;
  s.dvr.manage = false;
  s.checkRecorder();
  s.load(R"(
import QtQuick
import Jfd.Ui.Library
GuidePage { routeName: "guide"; route: "guide" }
)");
  auto* guide = s.root->property("guide").value<library::LiveTvGuide*>();
  REQUIRE(guide != nullptr);
  s.root->forceActiveFocus();
  REQUIRE(waitFor([&] { return guide->count() == Channels && !guide->loading(); }, 5000));
  QQuickItem* actions = find(s.root, "guideRecordActions");
  REQUIRE(actions != nullptr);
  CHECK_FALSE(actions->isVisible());
  CHECK(countNamed(s.root, "guideRecordings") == 0);
  s.key(Qt::Key_Menu);
  s.key(Qt::Key_R);
  waitFor([] { return false; }, 300);
  CHECK(s.visibleInWindow("popupEntry") == 0);
  CHECK(s.dvr.calls.isEmpty());

  Scene c;
  c.dvr.manage = false;
  c.checkRecorder();
  c.load(R"(
import QtQuick
import Jfd.Ui.Library
ChannelPage { routeName: "channel"; routeArgument: "c7"; route: "channel/c7" }
)");
  auto* schedule = c.root->property("schedule").value<library::LiveTvSchedule*>();
  REQUIRE(waitFor([&] { return !schedule->loading() && schedule->count() > 0; }, 5000));
  waitFor([] { return false; }, 200);
  CHECK(countNamed(c.root, "channelRecord") == 0);
}

TEST_CASE("A channel's page: the focused programme's Record button, R, and the mark") {
  Scene s;
  s.checkRecorder();
  s.load(R"(
import QtQuick
import Jfd.Ui.Library
ChannelPage { routeName: "channel"; routeArgument: "c7"; route: "channel/c7" }
)");
  auto* schedule = s.root->property("schedule").value<library::LiveTvSchedule*>();
  REQUIRE(schedule != nullptr);
  REQUIRE(waitFor([&] { return !schedule->loading() && schedule->count() > 2; }, 5000));
  s.root->forceActiveFocus();
  QQuickItem* watch = find(s.root, "channelWatch");
  REQUIRE(waitFor([&] { return watch->hasActiveFocus(); }, 2000));
  // Down to the programmes: the current one has a Record button.
  s.key(Qt::Key_Down);
  s.key(Qt::Key_Down);
  QQuickItem* list = find(s.root, "channelProgrammes");
  REQUIRE(list != nullptr);
  CHECK(list->property("currentIndex").toInt() == 1);
  REQUIRE(waitFor([&] { return countNamed(s.root, "channelRecord") == 1; }));
  CHECK(s.allNamed(s.root, "channelRecord").front()->property("text").toString() == QStringLiteral("Record"));

  s.key(Qt::Key_R);
  REQUIRE(waitFor([&] { return countNamed(s.root, "channelRecordingMark") == 1; }, 3000));
  CHECK(s.dvr.calls == QStringList{QStringLiteral("record:c7-3")});
  CHECK(schedule->data(schedule->index(1), library::LiveTvSchedule::TimerIdRole).toString() == QStringLiteral("t-c7-3"));
  // The recorded one keeps its button (Cancel recording) when the focus
  // moves on; the new current one has Record.
  s.key(Qt::Key_Down);
  REQUIRE(waitFor([&] { return countNamed(s.root, "channelRecord") == 2; }));
  QStringList texts;
  for (QQuickItem* b : s.allNamed(s.root, "channelRecord")) texts.append(b->property("text").toString());
  texts.sort();
  CHECK(texts == QStringList{QStringLiteral("Cancel recording"), QStringLiteral("Record")});
  s.screenshot("livetv-channel-recording");

  // The Menu key on the recorded one: Cancel recording.
  s.key(Qt::Key_Up);
  s.key(Qt::Key_Menu);
  REQUIRE(waitFor([&] { return s.visibleInWindow("popupEntry") == 1; }));
  REQUIRE(waitFor([&] { return s.focusedName() == QStringLiteral("popupEntry"); }));
  CHECK(s.window.activeFocusItem()->property("text").toString() == QStringLiteral("Cancel recording"));
  s.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return countNamed(s.root, "channelRecordingMark") == 0; }, 3000));
  CHECK(s.dvr.calls.last() == QStringLiteral("cancel:t-c7-3"));
  // The list has the focus again.
  REQUIRE(waitFor([&] { return list->hasActiveFocus(); }));
}

TEST_CASE("Recordings: a recording plays as an item, Delete asks first, recordings to come are cancelled") {
  Scene s;
  s.dvr.recordings = {
      R"({"Id": "r1", "Name": "Show 3.2", "Type": "Recording", "Status": "InProgress", "TimerId": "t1",
          "ChannelName": "Test Channel 3", "StartDate": "2026-11-15T00:30:00Z", "EndDate": "2026-11-15T01:00:00Z"})",
      R"({"Id": "r2", "Name": "Episode 4", "SeriesName": "Test Series", "Type": "Episode", "RunTimeTicks": 18000000000,
          "DateCreated": "2026-11-14T20:00:00Z", "UserData": {"PlaybackPositionTicks": 600000000}})",
      R"({"Id": "r3", "Name": "Show 1.1", "Type": "Movie", "RunTimeTicks": 18000000000,
          "DateCreated": "2026-11-13T20:00:00Z", "UserData": {"Played": true}})"};
  s.dvr.timers = {{QStringLiteral("t1"), QStringLiteral("c3-2"), {}, QStringLiteral("InProgress")},
                  {QStringLiteral("t9"), QStringLiteral("c4-5"), {}}};
  s.dvr.series = {QStringLiteral("s5")};
  s.checkRecorder();
  s.load(R"(
import QtQuick
import Jfd.Ui.Library
RecordingsPage { routeName: "recordings"; route: "recordings" }
)");
  auto* recordings = s.root->property("recordings").value<library::LiveTvRecordings*>();
  REQUIRE(recordings != nullptr);
  REQUIRE(waitFor([&] { return !recordings->loading() && recordings->count() == 3; }, 5000));
  REQUIRE(waitFor([&] { return s.recorder.count() == 3; }));
  s.root->forceActiveFocus();
  QQuickItem* list = find(s.root, "recordingList");
  QQuickItem* schedule = find(s.root, "scheduleList");
  REQUIRE(list != nullptr);
  REQUIRE(schedule != nullptr);
  REQUIRE(waitFor([&] { return list->hasActiveFocus(); }));
  REQUIRE(waitFor([&] { return countNamed(s.root, "recordingTitle") == 3 && countNamed(s.root, "timerTitle") == 3; }));
  s.screenshot("livetv-recordings");

  // Recording now first. It has no length yet: Enter goes to its Stop
  // button instead of playing it.
  CHECK(list->property("currentIndex").toInt() == 0);
  s.key(Qt::Key_Return);
  CHECK(s.played.isEmpty());
  CHECK(s.focusedName() == QStringLiteral("recordingDelete"));
  s.key(Qt::Key_Left);
  REQUIRE(waitFor([&] { return list->hasActiveFocus() && s.focusedName() != QStringLiteral("recordingDelete"); }));
  // The episode under its series.
  s.key(Qt::Key_Down);
  CHECK(list->property("currentIndex").toInt() == 1);
  // Enter plays it as an item, from where it was left.
  s.key(Qt::Key_Return);
  REQUIRE(s.played.size() == 1);
  CHECK(s.played.front() == QStringLiteral("r2"));
  CHECK(s.playedAt == 600000000);
  CHECK(s.tuned.isEmpty());

  // Right reaches the row's Delete; Enter asks; Esc keeps it.
  s.key(Qt::Key_Right);
  CHECK(s.focusedName() == QStringLiteral("recordingDelete"));
  s.key(Qt::Key_Return);
  auto* confirm = s.root->findChild<QObject*>(QStringLiteral("confirmDeleteRecording"));
  REQUIRE(confirm != nullptr);
  REQUIRE(waitFor([&] { return confirm->property("opened").toBool(); }));
  s.key(Qt::Key_Escape);
  REQUIRE(waitFor([&] { return !confirm->property("visible").toBool(); }));
  CHECK(s.dvr.calls.isEmpty());
  CHECK(recordings->count() == 3);

  // The Delete key on the row asks too; Enter deletes it and its file.
  s.key(Qt::Key_Left);
  s.key(Qt::Key_Delete);
  REQUIRE(waitFor([&] { return confirm->property("opened").toBool(); }));
  s.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return recordings->count() == 2; }, 3000));
  CHECK(s.dvr.calls == QStringList{QStringLiteral("delete:r2")});

  // The one recording now stops after its own question; it is not deleted.
  REQUIRE(waitFor([&] { return list->hasActiveFocus(); }));
  list->setProperty("currentIndex", 0);
  s.key(Qt::Key_Delete);
  auto* stop = s.root->findChild<QObject*>(QStringLiteral("confirmStopRecording"));
  REQUIRE(stop != nullptr);
  REQUIRE(waitFor([&] { return stop->property("opened").toBool(); }));
  s.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return s.dvr.calls.size() == 2; }, 3000));
  CHECK(s.dvr.calls.last() == QStringLiteral("cancel:t1"));

  // Down past the last recording reaches the schedule: the series first.
  REQUIRE(waitFor([&] { return s.recorder.count() == 2 && list->hasActiveFocus(); }, 3000));
  list->setProperty("currentIndex", recordings->count() - 1);
  s.key(Qt::Key_Down);
  REQUIRE(waitFor([&] { return schedule->hasActiveFocus(); }));
  CHECK(schedule->property("currentIndex").toInt() == 0);
  // Enter goes to its button; Enter again cancels the series.
  s.key(Qt::Key_Return);
  CHECK(s.focusedName() == QStringLiteral("timerCancel"));
  s.key(Qt::Key_Return);
  REQUIRE(waitFor([&] { return s.recorder.count() == 1; }, 3000));
  CHECK(s.dvr.calls.last() == QStringLiteral("cancelSeries:s5"));
  // Up from the schedule goes back to the recordings.
  REQUIRE(waitFor([&] { return schedule->hasActiveFocus(); }));
  schedule->setProperty("currentIndex", 0);
  s.key(Qt::Key_Up);
  CHECK(list->hasActiveFocus());
}

TEST_CASE("The Live TV screen links to Recordings only when this user may record") {
  for (const bool manage : {true, false}) {
    CAPTURE(manage);
    Scene s;
    s.dvr.manage = manage;
    s.checkRecorder();
    s.load(R"(
import QtQuick
import Jfd.Ui.Library
LiveTvPage { routeName: "livetv"; route: "livetv" }
)");
    QQuickItem* recordings = find(s.root, "liveTvRecordings");
    QQuickItem* guide = find(s.root, "liveTvGuide");
    REQUIRE(recordings != nullptr);
    REQUIRE(guide != nullptr);
    CHECK(recordings->isVisible() == manage);
    if (!manage) continue;
    // Left from Guide, then Enter.
    guide->forceActiveFocus();
    s.key(Qt::Key_Left);
    CHECK(recordings->hasActiveFocus());
    s.key(Qt::Key_Return);
    CHECK(s.navigator->currentRoute() == QStringLiteral("recordings"));
  }
}
