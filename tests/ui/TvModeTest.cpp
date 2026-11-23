// TV mode (stage 3, package 3.3): SpatialNavigation on synthetic screens
// and on the real shell, every focusable control answering Enter, the TV
// sizes and safe area, switching TV mode on a screen of 1000 cards, the
// gamepad check's state, the on-screen keyboard with text from the
// phone, and a gamepad without TV mode (focus-driven: the on-screen
// keyboard, "Show all" after a row's cards). Only arrows, Enter, Back and Menu (and a physical keyboard where
// the test says so).
#include "../support/DoctestQt.h"

#include <jfd/platform/InputAction.h>
#include <jfd/ui/GamepadStatus.h>
#include <jfd/ui/InputMode.h>
#include <jfd/ui/InputRouter.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/SpatialNavigation.h>
#include <jfd/ui/TextEntry.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)

using namespace jfd;
using ui::SpatialNavigation;

namespace {

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

  // A key tapped on the window, as a keyboard or InputRouter sends it.
  void tap(QQuickWindow& window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(&window, &release);
  }

  QString focusName(const QQuickWindow& window) {
    const QQuickItem* item = window.activeFocusItem();
    return item ? item->objectName() : QString();
  }

  // A singleton of Jfd.Ui.Shell, the module imported first so its types are
  // registered.
  template <class T> T* singleton(QQmlEngine& engine, const char* name) {
    QQmlComponent import(&engine);
    import.setData("import QtQml\nimport Jfd.Ui.Shell\nQtObject {}", QUrl(QStringLiteral("qrc:/import.qml")));
    std::unique_ptr<QObject> holder(import.create());
    return engine.singletonInstance<T*>(qmlTypeId("Jfd.Ui.Shell", 1, 0, name));
  }

  struct Scene {
    explicit Scene(const QByteArray& qml, QSize size = QSize(800, 600)) {
      window.resize(size);
      QQmlComponent component(&engine);
      component.setData(qml, QUrl(QStringLiteral("qrc:/tv-scene.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      root = qobject_cast<QQuickItem*>(object.get());
      REQUIRE(root != nullptr);
      root->setParentItem(window.contentItem());
      root->setSize(QSizeF(size));
      window.show();
      pump(30);
    }

    QQuickItem* item(const QString& name) const { return find(window.contentItem(), name); }
    void setTvMode(bool on) { QMetaObject::invokeMethod(object.get(), "setTvMode", Q_ARG(QVariant, on)); }

    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> object;
    QQuickItem* root = nullptr;
  };

  constexpr int Arrows[] = {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down};

  // A side column, rows of different length, a horizontal list, a
  // scrolled pane with an item below its fold, a grid of 200 whose
  // delegates are mostly not created, and a last row.
  constexpr auto GridSceneQml = R"(
import QtQuick
import Jfd.Ui.Shell
Item {
  id: root
  property int escapes: 0
  SpatialNavigation.enabled: true
  Keys.onEscapePressed: root.escapes++

  component Tile: Rectangle {
    width: 80; height: 50
    activeFocusOnTab: true
    color: activeFocus ? "orange" : "gray"
  }

  // Every scroll position back at the start.
  function reset() {
    pane.contentY = 0
    strip.positionViewAtBeginning()
    grid.positionViewAtBeginning()
  }
  // Makes the delegate exist and current.
  function showIndex(name, index) {
    const view = name === "strip" ? strip : grid
    view.currentIndex = index
    view.positionViewAtIndex(index, ListView.Contain)
  }

  Column {
    x: 10; y: 10; spacing: 10
    Repeater { model: 3; Tile { required property int index; objectName: "side" + index } }
  }
  Column {
    x: 120; y: 10; spacing: 10
    Row { spacing: 10; Repeater { model: 5; Tile { required property int index; objectName: "a" + index } } }
    Row { spacing: 10; Repeater { model: 3; Tile { required property int index; objectName: "b" + index } } }
    ListView {
      id: strip
      objectName: "strip"
      width: 260; height: 50
      orientation: ListView.Horizontal
      spacing: 10
      clip: true
      model: 12
      delegate: Tile { required property int index; objectName: "s" + index }
    }
    Flickable {
      id: pane
      objectName: "pane"
      width: 400; height: 120
      contentHeight: 600
      clip: true
      Tile { objectName: "near"; y: 10 }
      Tile { objectName: "far"; y: 500 }
    }
    GridView {
      id: grid
      objectName: "grid"
      width: 450; height: 120
      cellWidth: 90; cellHeight: 60
      cacheBuffer: 0
      clip: true
      model: 200
      delegate: Tile { required property int index; objectName: "g" + index }
    }
    Row { spacing: 10; Tile { objectName: "bottom" } }
  }
}
)";

  // The names of every node of the grid scene.
  QStringList gridSceneNodes() {
    QStringList names;
    for (int i = 0; i < 3; ++i) names << QStringLiteral("side%1").arg(i);
    for (int i = 0; i < 5; ++i) names << QStringLiteral("a%1").arg(i);
    for (int i = 0; i < 3; ++i) names << QStringLiteral("b%1").arg(i);
    for (int i = 0; i < 12; ++i) names << QStringLiteral("s%1").arg(i);
    names << QStringLiteral("near") << QStringLiteral("far");
    for (int i = 0; i < 200; ++i) names << QStringLiteral("g%1").arg(i);
    names << QStringLiteral("bottom");
    return names;
  }

  // Puts the focus on a node from a clean state (scrolled to the start), the
  // way a move would.
  QQuickItem* focusNode(Scene& scene, const QString& name) {
    QMetaObject::invokeMethod(scene.object.get(), "reset");
    const QChar kind = name.at(0);
    bool isIndex = false;
    const int index = name.mid(1).toInt(&isIndex);
    if (isIndex && (kind == QLatin1Char('s') || kind == QLatin1Char('g'))) {
      QMetaObject::invokeMethod(scene.object.get(), "showIndex",
                                Q_ARG(QVariant, kind == QLatin1Char('s') ? QStringLiteral("strip") : QStringLiteral("grid")),
                                Q_ARG(QVariant, index));
    }
    QQuickItem* item = scene.item(name);
    if (item) SpatialNavigation::focusItem(item);
    return item;
  }

  // Reachable nodes over the edges, from `start`.
  QSet<QString> reach(const std::map<QString, QSet<QString>>& edges, const QString& start) {
    QSet<QString> seen{start};
    std::deque<QString> queue{start};
    while (!queue.empty()) {
      const QString node = queue.front();
      queue.pop_front();
      const auto it = edges.find(node);
      if (it == edges.end()) continue;
      for (const QString& next : it->second) {
        if (seen.contains(next)) continue;
        seen.insert(next);
        queue.push_back(next);
      }
    }
    return seen;
  }

  // Resident memory in bytes (Linux; elsewhere 0, and the growth check is
  // skipped).
  qint64 residentBytes() {
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) return 0;
    const QList<QByteArray> fields = statm.readAll().split(' ');
    return fields.size() > 1 ? fields.at(1).toLongLong() * 4096 : 0;
  }

} // namespace

TEST_CASE("Arrows move to the nearest item: rows of different length, a list, a pane, a grid") {
  Scene scene(GridSceneQml);
  QQuickWindow& window = scene.window;

  REQUIRE(focusNode(scene, QStringLiteral("a0")) != nullptr);
  tap(window, Qt::Key_Right);
  CHECK(focusName(window) == QStringLiteral("a1"));
  // Down from past the end of a shorter row: its last item, when nothing
  // is right under the current one.
  focusNode(scene, QStringLiteral("a3"));
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("b2"));
  // Something right under it, further down, comes first.
  focusNode(scene, QStringLiteral("a4"));
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("g4"));
  // Left from a row: the side column, at the same height.
  focusNode(scene, QStringLiteral("b0"));
  tap(window, Qt::Key_Left);
  CHECK(focusName(window) == QStringLiteral("side1"));
  // Into the list, then along it by the list's own keys; the list's
  // current item follows the focus.
  tap(window, Qt::Key_Right);
  CHECK(focusName(window) == QStringLiteral("b0"));
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("s0"));
  tap(window, Qt::Key_Right);
  CHECK(focusName(window) == QStringLiteral("s1"));
  CHECK(scene.item(QStringLiteral("strip"))->property("currentIndex").toInt() == 1);
  // Up from the list lands in the row above, not back on the list.
  tap(window, Qt::Key_Up);
  CHECK(focusName(window).startsWith(QLatin1Char('b')));
}

TEST_CASE("An item outside the visible part of a pane is scrolled into view") {
  Scene scene(GridSceneQml);
  QQuickWindow& window = scene.window;
  QQuickItem* pane = scene.item(QStringLiteral("pane"));
  QQuickItem* far = scene.item(QStringLiteral("far"));
  REQUIRE(pane != nullptr);
  REQUIRE(far != nullptr);
  CHECK(pane->property("contentY").toDouble() == doctest::Approx(0.0));

  focusNode(scene, QStringLiteral("bottom"));
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("far"));
  const QRectF shown = far->mapRectToItem(pane, QRectF(0, 0, far->width(), far->height()));
  CHECK(pane->property("contentY").toDouble() > 300);
  CHECK(shown.top() >= 0);
  CHECK(shown.bottom() <= pane->height());
}

TEST_CASE("A grid's delegates that do not exist yet are reached through the grid's own keys") {
  Scene scene(GridSceneQml);
  QQuickWindow& window = scene.window;
  QQuickItem* grid = scene.item(QStringLiteral("grid"));
  REQUIRE(grid != nullptr);
  // Five columns, two rows visible, no cache: card 150 is not created.
  CHECK(scene.item(QStringLiteral("g150")) == nullptr);
  focusNode(scene, QStringLiteral("g0"));
  for (int i = 0; i < 30; ++i) tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("g150"));
  CHECK(grid->property("currentIndex").toInt() == 150);
  // And Up all the way back leaves the grid at its top.
  for (int i = 0; i < 31; ++i) tap(window, Qt::Key_Up);
  CHECK_FALSE(focusName(window).startsWith(QLatin1Char('g')));
}

TEST_CASE("No traps: every item reaches every other with arrows, and Back always gets out") {
  Scene scene(GridSceneQml);
  QQuickWindow& window = scene.window;
  const QStringList nodes = gridSceneNodes();
  const QSet<QString> all(nodes.begin(), nodes.end());

  // The graph of moves: each item by each arrow, from a clean state.
  std::map<QString, QSet<QString>> edges;
  std::map<QString, QSet<QString>> reverse;
  int escapesMissed = 0;
  for (const QString& node : nodes) {
    CAPTURE(node);
    for (const int key : Arrows) {
      REQUIRE(focusNode(scene, node) != nullptr);
      REQUIRE(focusName(window) == node);
      tap(window, key);
      const QString next = focusName(window);
      CHECK_MESSAGE(all.contains(next), "focus left the scene: " << next.toStdString());
      if (next != node) {
        edges[node].insert(next);
        reverse[next].insert(node);
      }
    }
    // Back from anywhere reaches the screen's own handler.
    focusNode(scene, node);
    const int before = scene.root->property("escapes").toInt();
    tap(window, Qt::Key_Escape);
    if (scene.root->property("escapes").toInt() != before + 1) ++escapesMissed;
  }
  CHECK(escapesMissed == 0);
  const QSet<QString> forward = reach(edges, QStringLiteral("a0"));
  const QSet<QString> backward = reach(reverse, QStringLiteral("a0"));
  for (const QString& node : nodes) {
    CAPTURE(node);
    CHECK_MESSAGE(forward.contains(node), "not reachable from a0");
    CHECK_MESSAGE(backward.contains(node), "a0 not reachable from it");
  }
}

TEST_CASE("Explicit KeyNavigation and the item's own keys come before the geometry") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Shell
Item {
  SpatialNavigation.enabled: true
  // KeyNavigation is mutual: far's Left goes back here.
  Rectangle { objectName: "left"; x: 10; y: 10; width: 80; height: 50; activeFocusOnTab: true; KeyNavigation.right: far }
  Rectangle { objectName: "middle"; x: 100; y: 10; width: 80; height: 50; activeFocusOnTab: true
              property int downs: 0
              Keys.onDownPressed: downs++ }
  Rectangle { id: far; objectName: "far"; x: 500; y: 10; width: 80; height: 50; activeFocusOnTab: true }
  Rectangle { objectName: "below"; x: 100; y: 100; width: 80; height: 50; activeFocusOnTab: true }
}
)");
  QQuickWindow& window = scene.window;
  SpatialNavigation::focusItem(scene.item(QStringLiteral("left")));
  tap(window, Qt::Key_Right);
  CHECK(focusName(window) == QStringLiteral("far"));
  tap(window, Qt::Key_Left);
  CHECK(focusName(window) == QStringLiteral("left"));
  // Geometry for the keys nobody else takes, the item's own Keys first.
  SpatialNavigation::focusItem(scene.item(QStringLiteral("far")));
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("below"));
  SpatialNavigation::focusItem(scene.item(QStringLiteral("middle")));
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("middle"));
  CHECK(scene.item(QStringLiteral("middle"))->property("downs").toInt() == 1);
}

TEST_CASE("PageDown and PageUp move about a page through a long list") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Shell
Item {
  SpatialNavigation.enabled: true
  ListView {
    objectName: "list"
    width: 400; height: 300
    clip: true
    model: 100
    delegate: Rectangle { required property int index; objectName: "row" + index; width: 400; height: 50
                          activeFocusOnTab: true; color: activeFocus ? "orange" : "gray" }
  }
}
)");
  QQuickWindow& window = scene.window;
  SpatialNavigation::focusItem(scene.item(QStringLiteral("row0")));
  tap(window, Qt::Key_PageDown);
  const int afterOne = focusName(window).mid(3).toInt();
  CHECK(afterOne >= 5);
  CHECK(afterOne <= 7);
  tap(window, Qt::Key_PageDown);
  CHECK(focusName(window).mid(3).toInt() >= 2 * afterOne - 1);
  tap(window, Qt::Key_PageUp);
  tap(window, Qt::Key_PageUp);
  CHECK(focusName(window) == QStringLiteral("row0"));
}

TEST_CASE("Every focusable control answers Enter, and Enter goes no further") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
Item {
  id: root
  property int leaked: 0
  property var answers: ({})
  function answered(name) { const a = root.answers; a[name] = (a[name] || 0) + 1; root.answers = a }
  Keys.onReturnPressed: root.leaked++
  Column {
    spacing: 4
    ActionButton { objectName: "ActionButton"; text: "Play"; onClicked: root.answered("ActionButton") }
    IconButton { objectName: "IconButton"; iconSource: Icons.play; onClicked: root.answered("IconButton") }
    ToggleIconButton { objectName: "ToggleIconButton"; iconSource: Icons.check; onClicked: root.answered("ToggleIconButton") }
    PosterCard { objectName: "PosterCard"; title: "Title"; onClicked: root.answered("PosterCard") }
    ListItem { objectName: "ListItem"; text: "Entry"; onClicked: root.answered("ListItem") }
    RoomCard { objectName: "RoomCard"; text: "Room"; onClicked: root.answered("RoomCard") }
    ToggleSwitch { objectName: "ToggleSwitch"; onToggled: root.answered("ToggleSwitch") }
    ChoiceBox {
      objectName: "ChoiceBox"
      model: [{ value: "a", label: "A" }, { value: "b", label: "B" }]
      onActivated: root.answered("ChoiceBox")
    }
    ValueSlider { objectName: "ValueSlider"; from: 0; to: 10; onAccepted: root.answered("ValueSlider") }
    InputField { objectName: "InputField"; onAccepted: root.answered("InputField") }
  }
}
)",
              QSize(800, 1000));
  QQuickWindow& window = scene.window;
  const auto answers = [&](const char* name) {
    return scene.root->property("answers").toMap().value(QLatin1String(name)).toInt();
  };
  const QStringList tested = {QStringLiteral("ActionButton"), QStringLiteral("IconButton"),
                              QStringLiteral("ToggleIconButton"),
                              QStringLiteral("PosterCard"),   QStringLiteral("ListItem"),
                              QStringLiteral("RoomCard"),
                              QStringLiteral("ToggleSwitch"), QStringLiteral("ChoiceBox"),
                              QStringLiteral("ValueSlider"),  QStringLiteral("InputField")};
  for (const QString& name : tested) {
    CAPTURE(name);
    // A text field passes Enter on to the form around it, by design (a
    // sign-in page signs in): checked for every control before it.
    if (name == QLatin1String("InputField")) CHECK(scene.root->property("leaked").toInt() == 0);
    QQuickItem* control = scene.item(name);
    REQUIRE(control != nullptr);
    control->forceActiveFocus(Qt::TabFocusReason);
    REQUIRE(window.activeFocusItem() != nullptr);
    tap(window, Qt::Key_Return);
    pump(20);
    if (name == QLatin1String("ChoiceBox")) {
      // Enter opens the list; Down and Enter choose the second entry.
      QObject* popup = control->property("popup").value<QObject*>();
      REQUIRE(popup != nullptr);
      CHECK(popup->property("visible").toBool());
      tap(window, Qt::Key_Down);
      tap(window, Qt::Key_Return);
      pump(250);
      CHECK_FALSE(popup->property("visible").toBool());
      CHECK(control->property("currentIndex").toInt() == 1);
    } else if (name == QLatin1String("ToggleSwitch")) {
      CHECK(control->property("checked").toBool());
    }
    CHECK(answers(name.toLatin1().constData()) == 1);
  }

  // Every focusable type of Jfd.Ui.Controls is in the list above, or is a
  // container whose cards answer for it.
  const QStringList containers = {QStringLiteral("CardRow"), QStringLiteral("CardGrid")};
  QFile qmldir(QStringLiteral(":/qt/qml/Jfd/Ui/Controls/qmldir"));
  REQUIRE(qmldir.open(QIODevice::ReadOnly));
  int types = 0;
  for (const QByteArray& rawLine : qmldir.readAll().split('\n')) {
    const QList<QByteArray> fields = rawLine.trimmed().split(' ');
    if (fields.size() != 3 || !fields.at(2).endsWith(".qml") || fields.at(0).isEmpty()) continue;
    if (!QChar::fromLatin1(fields.at(0).at(0)).isUpper()) continue;
    const QString type = QString::fromLatin1(fields.at(0));
    CAPTURE(type);
    QQmlComponent component(&scene.engine);
    component.setData("import QtQuick\nimport Jfd.Ui.Controls\nItem { " + fields.at(0) + " { } }",
                      QUrl(QStringLiteral("qrc:/type.qml")));
    std::unique_ptr<QObject> holder(component.create());
    if (!holder) continue;
    ++types;
    auto* item = qobject_cast<QQuickItem*>(holder.get());
    const QList<QQuickItem*> children = item ? item->childItems() : QList<QQuickItem*>();
    if (children.isEmpty() || !children.first()->activeFocusOnTab()) continue;
    CHECK_MESSAGE((tested.contains(type) || containers.contains(type)),
                  "a focusable control without an Enter check");
  }
  CHECK(types >= 15);
}

TEST_CASE("A drop-down list gives Up and Down to the focus, opens on Enter and closes on Back unchanged") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Controls
Item {
  id: root
  property int escapes: 0
  property int changes: 0
  function setTvMode(on) { Theme.tvMode = on }
  SpatialNavigation.enabled: true
  Keys.onEscapePressed: root.escapes++
  Column {
    spacing: 8
    ActionButton { objectName: "above"; text: "Above" }
    ChoiceBox {
      objectName: "choice"
      model: [{ value: "a", label: "A" }, { value: "b", label: "B" }, { value: "c", label: "C" }]
      currentIndex: 1
      onActivated: root.changes++
    }
    ActionButton { objectName: "below"; text: "Below" }
  }
}
)");
  QQuickWindow& window = scene.window;
  QQuickItem* choice = scene.item(QStringLiteral("choice"));
  REQUIRE(choice != nullptr);
  QObject* popup = choice->property("popup").value<QObject*>();
  REQUIRE(popup != nullptr);
  // A gamepad on the desktop, without TV mode, as well as in TV mode.
  for (const bool tv : {false, true}) {
    CAPTURE(tv);
    scene.setTvMode(tv);
    pump(20);
    SpatialNavigation::focusItem(scene.item(QStringLiteral("above")));
    // Passing over the list neither opens it nor changes it.
    tap(window, Qt::Key_Down);
    CHECK(focusName(window) == QStringLiteral("choice"));
    CHECK(choice->property("visualFocus").toBool());
    tap(window, Qt::Key_Down);
    CHECK(focusName(window) == QStringLiteral("below"));
    tap(window, Qt::Key_Up);
    CHECK(focusName(window) == QStringLiteral("choice"));
    tap(window, Qt::Key_Up);
    CHECK(focusName(window) == QStringLiteral("above"));
    CHECK_FALSE(popup->property("visible").toBool());
    CHECK(choice->property("currentIndex").toInt() == 1);

    // A opens it; there Up and Down are its entries; B closes it with no
    // change, the focus stays on it and the page does not go back.
    tap(window, Qt::Key_Down);
    tap(window, Qt::Key_Return);
    pump(20);
    REQUIRE(popup->property("visible").toBool());
    tap(window, Qt::Key_Down);
    CHECK(choice->property("highlightedIndex").toInt() == 2);
    tap(window, Qt::Key_Escape);
    pump(250);
    CHECK_FALSE(popup->property("visible").toBool());
    CHECK(choice->property("currentIndex").toInt() == 1);
    CHECK(focusName(window) == QStringLiteral("choice"));
    CHECK(scene.root->property("escapes").toInt() == 0);
    // And the arrows leave it again.
    tap(window, Qt::Key_Down);
    CHECK(focusName(window) == QStringLiteral("below"));
    CHECK(scene.root->property("changes").toInt() == 0);

    // A, Down and A choose.
    SpatialNavigation::focusItem(choice);
    tap(window, Qt::Key_Return);
    pump(20);
    tap(window, Qt::Key_Down);
    tap(window, Qt::Key_Return);
    pump(250);
    CHECK_FALSE(popup->property("visible").toBool());
    CHECK(choice->property("currentIndex").toInt() == 2);
    CHECK(scene.root->property("changes").toInt() == 1);
    choice->setProperty("currentIndex", 1);
    scene.root->setProperty("changes", 0);
  }
}

namespace {

  // A settings list: 30 rows of 50 px in a 300 px list, each a drop-down
  // that counts its changes (as SettingRow saves them at once).
  constexpr auto SettingsListQml = R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Controls
Item {
  property int changes: 0
  function setTvMode(on) { Theme.tvMode = on }
  SpatialNavigation.enabled: true
  ListView {
    objectName: "settings"
    width: 400; height: 300
    clip: true
    focus: true
    model: 30
    delegate: FocusScope {
      required property int index
      objectName: "setting" + index
      width: 400; height: 50
      ChoiceBox {
        objectName: "choice" + index
        focus: true
        model: [{ value: "a", label: "A" }, { value: "b", label: "B" }, { value: "c", label: "C" }]
        currentIndex: 1
        onActivated: changes++
        onCurrentIndexChanged: if (currentIndex !== 1) changes++
      }
    }
  }
}
)";

} // namespace

TEST_CASE("PageDown over a drop-down list moves the focus a page and never changes its value") {
  Scene scene(SettingsListQml);
  QQuickWindow& window = scene.window;
  QQuickItem* first = scene.item(QStringLiteral("choice0"));
  REQUIRE(first != nullptr);
  SpatialNavigation::focusItem(first);
  REQUIRE(focusName(window) == QStringLiteral("choice0"));

  // TV mode off: the drop-down's own Up and Down would change the value.
  tap(window, Qt::Key_PageDown);
  const int paged = focusName(window).mid(6).toInt();
  CHECK(focusName(window).startsWith(QLatin1String("choice")));
  CHECK(paged >= 5);
  CHECK(scene.item(QStringLiteral("settings"))->property("currentIndex").toInt() == paged);
  tap(window, Qt::Key_PageUp);
  CHECK(focusName(window) == QStringLiteral("choice0"));
  CHECK(first->property("currentIndex").toInt() == 1);
  CHECK(scene.root->property("changes").toInt() == 0);
}

TEST_CASE("Down from the last visible drop-down goes on to the setting below the list's edge, in TV mode or not") {
  for (const bool tv : {false, true}) {
  CAPTURE(tv);
  Scene scene(SettingsListQml);
  QQuickWindow& window = scene.window;
  QQuickItem* list = scene.item(QStringLiteral("settings"));
  REQUIRE(list != nullptr);
  scene.setTvMode(tv);
  pump(20);
  // Rows 0 to 5 fill the list; row 6 is under its edge.
  SpatialNavigation::focusItem(scene.item(QStringLiteral("choice5")));
  REQUIRE(focusName(window) == QStringLiteral("choice5"));
  for (int row = 6; row <= 12; ++row) {
    CAPTURE(row);
    tap(window, Qt::Key_Down);
    REQUIRE(focusName(window) == QStringLiteral("choice%1").arg(row));
    // The row is what the list scrolls into view (a TV-sized control may be taller than it).
    QQuickItem* shownRow = scene.item(QStringLiteral("setting%1").arg(row));
    REQUIRE(shownRow != nullptr);
    const QRectF shown = shownRow->mapRectToItem(list, QRectF(0, 0, shownRow->width(), shownRow->height()));
    CHECK(shown.top() >= -0.5);
    CHECK(shown.bottom() <= list->height() + 0.5);
  }
  tap(window, Qt::Key_Up);
  CHECK(focusName(window) == QStringLiteral("choice11"));
  CHECK(scene.root->property("changes").toInt() == 0);
  }
}

namespace {

  // Home: a button above, then rows of cards (CardRow in a vertical list),
  // each row remembering its own card.
  constexpr auto CardRowsQml = R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Controls
Item {
  function setTvMode(on) { Theme.tvMode = on }
  SpatialNavigation.enabled: true
  Column {
    width: parent.width
    ActionButton {
      objectName: "top"
      text: "Top"
      x: 300
      KeyNavigation.down: rows
    }
    ListView {
      id: rows
      objectName: "rows"
      width: parent.width; height: 560
      clip: true
      spacing: 20
      keyNavigationEnabled: true
      model: 4
      delegate: CardRow {
        id: row
        required property int index
        objectName: "row" + index
        width: rows.width
        focus: ListView.isCurrentItem
        cardHeight: 150
        model: 12
        delegate: PosterCard {
          required property int index
          objectName: "r" + row.index + "c" + index
          title: "Card " + index
          cardWidth: 90
          shape: "thumb"
        }
      }
    }
  }
}
)";

  int column(const QQuickWindow& window) { return focusName(window).section(QLatin1Char('c'), 1).toInt(); }
  int rowOf(const QQuickWindow& window) { return focusName(window).mid(1).section(QLatin1Char('c'), 0, 0).toInt(); }

} // namespace

TEST_CASE("Up and Down between rows of cards go straight, and the focused card shows it") {
  for (const bool tv : {false, true}) {
    CAPTURE(tv);
    Scene scene(CardRowsQml, QSize(1000, 700));
    scene.setTvMode(tv);
    pump(30);
    QQuickWindow& window = scene.window;
    // The second row remembers its 7th card from before.
    scene.item(QStringLiteral("row1"))->setProperty("currentIndex", 7);
    SpatialNavigation::focusItem(scene.item(QStringLiteral("r0c0")));
    REQUIRE(focusName(window) == QStringLiteral("r0c0"));
    // Right along the row (the row's own keys): each card shows the focus.
    for (int i = 1; i <= 2; ++i) {
      tap(window, Qt::Key_Right);
      pump(10);
      CHECK(column(window) == i);
      CHECK(window.activeFocusItem()->property("visualFocus").toBool());
    }
    const auto centreX = [&]() {
      QQuickItem* item = window.activeFocusItem();
      return item->mapToScene(QPointF(item->width() / 2, 0)).x();
    };
    double before = centreX();
    // Down: the card under it, not the remembered one.
    tap(window, Qt::Key_Down);
    pump(10);
    CHECK(rowOf(window) == 1);
    CHECK(column(window) == 2);
    CHECK(std::abs(centreX() - before) < 30);
    CHECK(window.activeFocusItem()->property("visualFocus").toBool());
    tap(window, Qt::Key_Down);
    pump(10);
    CHECK(rowOf(window) == 2);
    CHECK(column(window) == 2);
    tap(window, Qt::Key_Right);
    pump(300);
    before = centreX();
    tap(window, Qt::Key_Up);
    pump(10);
    CHECK(rowOf(window) == 1);
    CHECK(std::abs(centreX() - before) < 30);
    CHECK(window.activeFocusItem()->property("visualFocus").toBool());
    // From the button above the rows: the card under the button.
    SpatialNavigation::focusItem(scene.item(QStringLiteral("top")));
    scene.item(QStringLiteral("rows"))->setProperty("currentIndex", 0);
    scene.item(QStringLiteral("row0"))->setProperty("currentIndex", 0);
    pump(300);
    before = centreX();
    tap(window, Qt::Key_Down);
    pump(10);
    CHECK(rowOf(window) == 0);
    CHECK(std::abs(centreX() - before) < 60);
    CHECK(window.activeFocusItem()->property("visualFocus").toBool());
  }
}

TEST_CASE("In TV mode PageDown goes through a card grid to its end, scrolling, and arrows still leave at its edges") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Controls
Item {
  id: root
  function setTvMode(on) { Theme.tvMode = on }
  SpatialNavigation.enabled: true
  Rectangle { objectName: "side"; x: 0; y: 10; width: 80; height: 50; activeFocusOnTab: true }
  CardGrid {
    objectName: "grid"
    x: 100; width: 700; height: 600
    model: 200
    delegate: Rectangle {
      required property int index
      objectName: "card" + index
      width: 150; height: 220
      activeFocusOnTab: true
    }
  }
}
)");
  QQuickWindow& window = scene.window;
  scene.setTvMode(true);
  pump(20);
  auto* grid = qvariant_cast<QQuickItem*>(scene.item(QStringLiteral("grid"))->property("view"));
  REQUIRE(grid != nullptr);
  const int columns = grid->property("columns").toInt();
  REQUIRE(columns > 1);
  REQUIRE(grid->property("keyNavigationEnabled").toBool() == false);
  SpatialNavigation::focusItem(scene.item(QStringLiteral("card0")));
  REQUIRE(focusName(window) == QStringLiteral("card0"));
  const auto cardIndex = [&window] { return focusName(window).mid(4).toInt(); };
  int last = 0;
  int presses = 0;
  for (; presses < 100; ++presses) {
    tap(window, Qt::Key_PageDown);
    pump(5);
    REQUIRE(focusName(window).startsWith(QStringLiteral("card")));
    if (cardIndex() == last) break;
    CHECK(cardIndex() > last);
    last = cardIndex();
    QQuickItem* focused = window.activeFocusItem();
    const QRectF shown = focused->mapRectToItem(grid, QRectF(0, 0, focused->width(), focused->height()));
    CHECK(shown.top() >= -0.5);
    CHECK(shown.bottom() <= grid->height() + 0.5);
  }
  CHECK(presses > 1);
  CHECK(last >= 200 - columns);
  CHECK(grid->property("contentY").toDouble() > grid->height());
  CHECK(grid->property("currentIndex").toInt() == last);
  // Back to the top by pages.
  for (int i = 0; i < 100 && cardIndex() >= columns; ++i) tap(window, Qt::Key_PageUp);
  CHECK(cardIndex() < columns);
  // Plain arrows keep the TV edges: Left from the first column leaves the
  // grid instead of wrapping to the row above.
  SpatialNavigation::focusItem(scene.item(QStringLiteral("card%1").arg(columns)));
  pump(5);
  REQUIRE(focusName(window) == QStringLiteral("card%1").arg(columns));
  tap(window, Qt::Key_Left);
  CHECK(focusName(window) == QStringLiteral("side"));
}

TEST_CASE("In TV mode text and controls are TV-sized, and switch back without a restart") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
Item {
  function setTvMode(on) { Theme.tvMode = on }
  property int body: Theme.fontSizeBody
  property int small: Theme.fontSizeSmall
  property int duration: Theme.durationNormal
  Column {
    ActionButton { objectName: "ActionButton"; text: "Play" }
    IconButton { objectName: "IconButton"; iconSource: Icons.play }
    ListItem { objectName: "ListItem"; text: "Entry" }
    ToggleSwitch { objectName: "ToggleSwitch" }
    ChoiceBox { objectName: "ChoiceBox"; model: [{ value: "a", label: "A" }] }
    ValueSlider { objectName: "ValueSlider" }
    InputField { objectName: "InputField" }
  }
}
)",
              QSize(1920, 1080));
  const QStringList controls = {QStringLiteral("ActionButton"), QStringLiteral("IconButton"),
                                QStringLiteral("ListItem"),     QStringLiteral("ToggleSwitch"),
                                QStringLiteral("ChoiceBox"),    QStringLiteral("ValueSlider"),
                                QStringLiteral("InputField")};
  const int desktopDuration = scene.root->property("duration").toInt();
  CHECK(scene.root->property("body").toInt() < 24);

  scene.setTvMode(true);
  pump(20);
  CHECK(scene.root->property("body").toInt() >= 24);
  CHECK(scene.root->property("small").toInt() >= 18);
  CHECK(scene.root->property("duration").toInt() < desktopDuration);
  for (const QString& name : controls) {
    CAPTURE(name);
    QQuickItem* control = scene.item(name);
    REQUIRE(control != nullptr);
    CHECK(control->height() >= 64);
    if (name == QLatin1String("IconButton") || name == QLatin1String("ToggleSwitch")) CHECK(control->width() >= 64);
  }

  scene.setTvMode(false);
  pump(20);
  CHECK(scene.root->property("body").toInt() < 24);
  CHECK(scene.item(QStringLiteral("ActionButton"))->height() < 64);
}

TEST_CASE("Switching TV mode with a thousand cards on screen is quick and does not grow memory") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
Item {
  function setTvMode(on) { Theme.tvMode = on }
  CardGrid {
    anchors.fill: parent
    model: 1000
    delegate: PosterCard { title: "Title " + index; subtitle: "2024" }
  }
}
)",
              QSize(1920, 1080));
  QQuickWindow& window = scene.window;
  window.grabWindow();
  // The cards change size, so the grid creates and drops delegates: what an
  // event loop does between frames (deleteLater) and the JavaScript
  // collector's next run are let happen, so only memory that stays counts.
  const auto settle = [&scene] {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    scene.engine.collectGarbage();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  // The first switches allocate caches that stay; the 20 that count follow.
  for (int i = 0; i < 10; ++i) {
    scene.setTvMode(i % 2 == 0);
    QCoreApplication::processEvents();
    window.grabWindow();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  settle();
  const qint64 before = residentBytes();
  qint64 slowest = 0;
  for (int i = 0; i < 20; ++i) {
    QElapsedTimer timer;
    timer.start();
    scene.setTvMode(i % 2 == 0);
    QCoreApplication::processEvents();
    window.grabWindow();
    slowest = std::max(slowest, timer.elapsed());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  settle();
  const qint64 growth = residentBytes() - before;
  MESSAGE("slowest TV mode switch ", slowest, " ms, memory growth ", growth / 1024, " KiB");
  // ASan slows Qt Quick several times and holds freed memory in quarantine, so
  // neither the time nor the resident size means anything under it.
#if defined(__SANITIZE_ADDRESS__)
  constexpr bool measurable = false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
  constexpr bool measurable = false;
#else
  constexpr bool measurable = true;
#endif
#else
  constexpr bool measurable = true;
#endif
  if (measurable) CHECK(slowest <= 100);
  if (measurable && before > 0) CHECK(growth <= 5 * 1024 * 1024);
}

namespace {

  // A test page of twelve buttons.
  constexpr auto ButtonsPageQml = R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
RoutePage {
  Grid {
    x: Theme.pageMargin; y: Theme.spacingHuge
    columns: 4
    spacing: Theme.spacing
    Repeater {
      model: 12
      ActionButton { required property int index; objectName: "button" + index; text: "Button " + index; focus: index === 0 }
    }
  }
}
)";

  // The real shell on a test page (the buttons by default), at 1920x1080.
  struct ShellScene {
    explicit ShellScene(const char* pageQml = ButtonsPageQml) {
      REQUIRE(pages.isValid());
      const QString page = pages.filePath(QStringLiteral("TestPage.qml"));
      QFile file(page);
      REQUIRE(file.open(QIODevice::WriteOnly));
      file.write(pageQml);
      file.close();
      navigator = singleton<ui::Navigator>(engine, "Navigator");
      REQUIRE(navigator != nullptr);
      navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      navigator->registerPage(QStringLiteral("home"), QUrl::fromLocalFile(page));
      navigator->reset(QStringLiteral("home"));

      window.resize(1920, 1080);
      QQmlComponent component(&engine);
      component.setData(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
AppShell {
  function setTvMode(on) { Theme.tvMode = on }
  function setSafeArea(on) { Theme.tvSafeArea = on }
}
)",
                        QUrl(QStringLiteral("qrc:/tv-shell.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      shell = qobject_cast<QQuickItem*>(object.get());
      shell->setParentItem(window.contentItem());
      shell->setSize(QSizeF(1920, 1080));
      window.show();
      pump(400);
    }

    // The input mode is one for the process: the next test starts from a
    // keyboard.
    ~ShellScene() { ui::InputMode::instance()->setGamepad(false); }

    QQuickItem* item(const QString& name) const { return find(window.contentItem(), name); }
    void setTvMode(bool on) { QMetaObject::invokeMethod(object.get(), "setTvMode", Q_ARG(QVariant, on)); }
    void setSafeArea(bool on) { QMetaObject::invokeMethod(object.get(), "setSafeArea", Q_ARG(QVariant, on)); }

    // The visible, enabled items that take the focus by keys.
    QList<QQuickItem*> focusable() const {
      QList<QQuickItem*> found;
      std::function<void(QQuickItem*)> walk = [&](QQuickItem* node) {
        if (!node->isVisible() || !node->isEnabled()) return;
        if (node->activeFocusOnTab() && node->width() > 0 && node->height() > 0) found.append(node);
        const QList<QQuickItem*> children = node->childItems();
        for (QQuickItem* child : children) walk(child);
      };
      walk(shell);
      return found;
    }

    QTemporaryDir pages;
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QObject> object;
    QQuickItem* shell = nullptr;
    ui::Navigator* navigator = nullptr;
  };

} // namespace

TEST_CASE("The shell in TV mode: safe area, every item reachable, Menu goes to the navigation bar and back") {
  ShellScene scene;
  QQuickWindow& window = scene.window;
  scene.setTvMode(true);
  pump(300);

  // Nothing that takes the focus in the outer 5 % of the screen.
  const QRectF safe(96, 54, 1920 - 2 * 96, 1080 - 2 * 54);
  const QList<QQuickItem*> focusable = scene.focusable();
  CHECK(focusable.size() >= 12 + 4);
  for (QQuickItem* item : focusable) {
    CAPTURE(item->objectName());
    const QRectF rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    CHECK(safe.adjusted(-0.5, -0.5, 0.5, 0.5).contains(rect));
  }
  QQuickItem* stack = scene.item(QStringLiteral("pageStack"));
  REQUIRE(stack != nullptr);
  scene.setSafeArea(false);
  pump(50);
  CHECK(stack->mapRectToScene(QRectF(0, 0, stack->width(), stack->height())).right() == doctest::Approx(1920));
  scene.setSafeArea(true);
  pump(50);

  // From the first button: Up to the bar, at the current page's entry
  // whichever is nearest; Down back to the page where the focus was.
  QQuickItem* first = scene.item(QStringLiteral("button0"));
  REQUIRE(first != nullptr);
  SpatialNavigation::focusItem(first);
  tap(window, Qt::Key_Up);
  CHECK(focusName(window) == QStringLiteral("nav_home"));
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("button0"));

  // Menu: the bar's entry of the current page; Menu again: the page.
  SpatialNavigation::focusItem(scene.item(QStringLiteral("button5")));
  tap(window, Qt::Key_Menu);
  CHECK(focusName(window) == QStringLiteral("nav_home"));
  tap(window, Qt::Key_Menu);
  CHECK(focusName(window).startsWith(QLatin1String("button")));

  // Every focusable item reaches every other by arrows (a scope such as
  // the bar passes the focus on to its entry: not a node of its own).
  std::map<QString, QSet<QString>> edges;
  std::map<QString, QSet<QString>> reverse;
  QSet<QString> names;
  QList<QQuickItem*> nodes;
  for (QQuickItem* item : focusable) {
    SpatialNavigation::focusItem(item);
    if (item->objectName().isEmpty() || focusName(window) != item->objectName()) continue;
    names.insert(item->objectName());
    nodes.append(item);
  }
  CHECK(names.contains(QStringLiteral("nav_home")));
  CHECK(names.contains(QStringLiteral("button11")));
  for (QQuickItem* item : std::as_const(nodes)) {
    const QString name = item->objectName();
    for (const int key : Arrows) {
      SpatialNavigation::focusItem(item);
      REQUIRE(focusName(window) == name);
      tap(window, key);
      const QString next = focusName(window);
      if (next != name && names.contains(next)) {
        edges[name].insert(next);
        reverse[next].insert(name);
      }
    }
  }
  const QSet<QString> forward = reach(edges, QStringLiteral("button0"));
  const QSet<QString> backward = reach(reverse, QStringLiteral("button0"));
  for (const QString& name : std::as_const(names)) {
    CAPTURE(name);
    CHECK(forward.contains(name));
    CHECK(backward.contains(name));
  }

  // Back leaves a page opened over home, from anywhere on it.
  REQUIRE(scene.navigator->navigate(QStringLiteral("settings")));
  pump(300);
  CHECK(scene.navigator->depth() == 2);
  tap(window, Qt::Key_Escape);
  pump(300);
  CHECK(scene.navigator->depth() == 1);
}

TEST_CASE("The gamepad check shows each press instead of acting, and Back twice ends it") {
  ui::GamepadStatus status;
  using platform::InputAction;
  CHECK_FALSE(status.takePress(InputAction::Select, false));
  status.setAvailable(true);
  status.setControllers({QStringLiteral("Xbox Series X Controller")});
  CHECK(status.connected());
  CHECK(status.controllerName() == QStringLiteral("Xbox Series X Controller"));

  status.startCheck();
  CHECK(status.checking());
  CHECK(status.pressCount() == 0);
  CHECK(status.takePress(InputAction::Select, false));
  CHECK(status.lastAction() == QStringLiteral("Select"));
  CHECK(status.takePress(InputAction::Back, false));
  CHECK(status.checking());
  // A held direction repeats: taken, but counted once.
  CHECK(status.takePress(InputAction::Down, false));
  CHECK(status.takePress(InputAction::Down, true));
  CHECK(status.pressCount() == 3);
  CHECK(status.takePress(InputAction::Back, false));
  CHECK(status.takePress(InputAction::Back, false));
  CHECK_FALSE(status.checking());
  CHECK_FALSE(status.takePress(InputAction::Select, false));

  status.setControllers({});
  CHECK_FALSE(status.connected());
}

namespace {

  // Two fields between two buttons: Enter in the name goes on to the
  // secret, Enter in the secret submits.
  constexpr auto FieldsPageQml = R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
RoutePage {
  id: page
  objectName: "fieldsPage"
  property int submitted: 0
  Column {
    x: Theme.pageMargin; y: Theme.spacingHuge
    width: 600
    spacing: Theme.spacing
    ActionButton { objectName: "before"; text: "Before"; focus: true }
    InputField { id: name; objectName: "nameField"; width: parent.width; label: "Name"; onAccepted: secret.forceActiveFocus() }
    InputField { id: secret; objectName: "secretField"; width: parent.width; label: "Secret"; password: true; onAccepted: page.submitted++ }
    ActionButton { objectName: "after"; text: "After" }
  }
}
)";

  struct KeyboardScene : ShellScene {
    KeyboardScene() : ShellScene(FieldsPageQml) {
      entry = qobject_cast<ui::TextEntry*>(item(QStringLiteral("textEntry")));
      REQUIRE(entry != nullptr);
      setTvMode(true);
      pump(300);
    }

    // The TextInput inside an InputField.
    QQuickItem* input(const char* name) const {
      QQuickItem* field = item(QString::fromLatin1(name));
      return field ? field->property("field").value<QQuickItem*>() : nullptr;
    }
    QString text(const char* name) const { return input(name)->property("text").toString(); }
    QQuickItem* keyboard() const { return item(QStringLiteral("onScreenKeyboard")); }
    // The highlighted key's name.
    QString highlighted() const {
      QQuickItem* board = keyboard();
      const int row = board->property("row").toInt();
      const int column = board->property("column").toInt();
      const QVariantList rows = board->property("rows").toList();
      const QVariantMap key = rows.at(row).toList().at(column).toMap();
      return key.contains(QStringLiteral("action")) ? key.value(QStringLiteral("action")).toString()
                                                     : key.value(QStringLiteral("text")).toString();
    }
    int submitted() const { return item(QStringLiteral("fieldsPage"))->property("submitted").toInt(); }
    // Presses the arrows until the key is highlighted (from the bottom row
    // up, never past the top: Up there closes the keyboard); false if it is
    // not on the keyboard.
    bool highlight(const QString& name) {
      if (!entry->isOpen()) return false;
      for (int i = 0; i < 6; ++i) tap(window, Qt::Key_Down);
      while (true) {
        for (int i = 0; i < 14; ++i) tap(window, Qt::Key_Left);
        for (int i = 0; i < 14; ++i) {
          if (highlighted() == name) return true;
          tap(window, Qt::Key_Right);
        }
        if (keyboard()->property("row").toInt() == 0) return false;
        tap(window, Qt::Key_Up);
      }
    }
    void typeKey(const QString& name) {
      REQUIRE(highlight(name));
      tap(window, Qt::Key_Return);
    }

    ui::TextEntry* entry = nullptr;
  };

} // namespace

TEST_CASE("In TV mode a field reached by arrows opens the on-screen keyboard, which arrows, Select and Back drive") {
  KeyboardScene scene;
  QQuickWindow& window = scene.window;
  SpatialNavigation::focusItem(scene.item(QStringLiteral("before")));
  CHECK_FALSE(scene.entry->isOpen());

  tap(window, Qt::Key_Down);
  REQUIRE(window.activeFocusItem() == scene.input("nameField"));
  CHECK(scene.entry->isOpen());
  CHECK(scene.entry->field() == scene.input("nameField"));
  REQUIRE(scene.keyboard() != nullptr);
  CHECK(scene.keyboard()->isVisible());
  // Above the dialogs and the command palette.
  CHECK(scene.keyboard()->parentItem()->inherits("QQuickOverlay"));
  CHECK(scene.highlighted() == QStringLiteral("q"));
  // Its keys are TV-sized and inside the safe area.
  const QRectF safe(96, 54, 1920 - 2 * 96, 1080 - 2 * 54);
  int keys = 0;
  std::function<void(QQuickItem*)> walk = [&](QQuickItem* node) {
    if (node->objectName().startsWith(QLatin1String("key_"))) {
      ++keys;
      CAPTURE(node->objectName());
      CHECK(node->height() >= 64);
      CHECK(safe.contains(node->mapRectToScene(QRectF(0, 0, node->width(), node->height()))));
    }
    const QList<QQuickItem*> children = node->childItems();
    for (QQuickItem* child : children) walk(child);
  };
  walk(scene.keyboard());
  CHECK(keys == 10 + 10 + 9 + 10 + 8);

  // Right twice, Select: "e"; Up to the digits, Select: "3". The focus
  // never leaves the field.
  tap(window, Qt::Key_Right);
  tap(window, Qt::Key_Right);
  tap(window, Qt::Key_Return);
  tap(window, Qt::Key_Up);
  CHECK(scene.highlighted() == QStringLiteral("3"));
  tap(window, Qt::Key_Return);
  CHECK(scene.text("nameField") == QStringLiteral("e3"));
  CHECK(window.activeFocusItem() == scene.input("nameField"));

  // Shift gives one capital; the other language; the symbols.
  scene.typeKey(QStringLiteral("shift"));
  scene.typeKey(QStringLiteral("z"));
  scene.typeKey(QStringLiteral("z"));
  CHECK(scene.text("nameField") == QStringLiteral("e3Zz"));
  scene.typeKey(QStringLiteral("language"));
  scene.typeKey(QString::fromUtf8("й"));
  scene.typeKey(QStringLiteral("mode"));
  scene.typeKey(QStringLiteral("@"));
  scene.typeKey(QStringLiteral("mode"));
  scene.typeKey(QString::fromUtf8("ё"));
  CHECK(scene.text("nameField") == QString::fromUtf8("e3Zzй@ё"));

  // The cursor, erase and space; the gamepad's shoulders erase and space.
  scene.typeKey(QStringLiteral("left"));
  scene.typeKey(QStringLiteral("erase"));
  CHECK(scene.text("nameField") == QString::fromUtf8("e3Zzйё"));
  scene.typeKey(QStringLiteral("right"));
  scene.typeKey(QStringLiteral("space"));
  tap(window, Qt::Key_PageDown);
  tap(window, Qt::Key_PageUp);
  CHECK(scene.text("nameField") == QString::fromUtf8("e3Zzйё "));

  // Up from the digits closes it, back on the field; Select opens it.
  for (int i = 0; i < 6 && scene.entry->isOpen(); ++i) tap(window, Qt::Key_Up);
  CHECK_FALSE(scene.entry->isOpen());
  CHECK(window.activeFocusItem() == scene.input("nameField"));
  tap(window, Qt::Key_Return);
  CHECK(scene.entry->isOpen());
  // On the first letter, of the language last used.
  CHECK(scene.highlighted() == QString::fromUtf8("й"));

  // Back closes it and stays on the page; then the arrows move on.
  const int depth = scene.navigator->depth();
  tap(window, Qt::Key_Escape);
  CHECK_FALSE(scene.entry->isOpen());
  CHECK(scene.navigator->depth() == depth);
  CHECK(window.activeFocusItem() == scene.input("nameField"));
  tap(window, Qt::Key_Down);
  CHECK(window.activeFocusItem() == scene.input("secretField"));
  CHECK(scene.entry->isOpen());
  tap(window, Qt::Key_Escape);
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("after"));
  CHECK_FALSE(scene.entry->isOpen());
}

TEST_CASE("Done is Enter in the field: the keyboard follows to the next field, then the form is sent") {
  KeyboardScene scene;
  QQuickWindow& window = scene.window;
  // A field the page focuses itself does not open the keyboard; Select does.
  scene.input("nameField")->forceActiveFocus();
  CHECK_FALSE(scene.entry->isOpen());
  tap(window, Qt::Key_Return);
  REQUIRE(scene.entry->isOpen());
  scene.typeKey(QStringLiteral("a"));
  scene.typeKey(QStringLiteral("done"));
  CHECK(scene.text("nameField") == QStringLiteral("a"));
  CHECK(window.activeFocusItem() == scene.input("secretField"));
  CHECK(scene.entry->isOpen());
  CHECK(scene.highlighted() == QStringLiteral("q"));
  // A hidden field shows its mask above the keys.
  scene.typeKey(QStringLiteral("b"));
  QQuickItem* preview = scene.item(QStringLiteral("keyboardPreview"));
  REQUIRE(preview != nullptr);
  CHECK_FALSE(preview->property("text").toString().contains(QLatin1Char('b')));
  scene.typeKey(QStringLiteral("done"));
  CHECK(scene.text("secretField") == QStringLiteral("b"));
  CHECK(scene.submitted() == 1);
  CHECK_FALSE(scene.entry->isOpen());

  // A click on a key types as well, and the field keeps the focus.
  tap(window, Qt::Key_Return);
  REQUIRE(scene.entry->isOpen());
  QQuickItem* key = scene.item(QStringLiteral("key_c"));
  REQUIRE(key != nullptr);
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                    key->mapToScene(QPointF(key->width() / 2, key->height() / 2)).toPoint());
  CHECK(scene.text("secretField") == QStringLiteral("bc"));
  CHECK(window.activeFocusItem() == scene.input("secretField"));
  CHECK(scene.entry->isOpen());

  // A field that keeps Select for itself (the command palette on a chosen
  // result): Enter is Enter.
  tap(window, Qt::Key_Escape);
  scene.item(QStringLiteral("nameField"))->setProperty("keyboardOnSelect", false);
  scene.input("nameField")->forceActiveFocus();
  tap(window, Qt::Key_Return);
  CHECK_FALSE(scene.entry->isOpen());
  CHECK(window.activeFocusItem() == scene.input("secretField"));
}

TEST_CASE("A physical keyboard that types keeps the on-screen keyboard away until the gamepad is used again") {
  KeyboardScene scene;
  QQuickWindow& window = scene.window;
  window.requestActivate();
  REQUIRE(QTest::qWaitForWindowActive(&window));
  SpatialNavigation::focusItem(scene.item(QStringLiteral("before")));
  tap(window, Qt::Key_Down);
  REQUIRE(scene.entry->isOpen());

  // Typing on the keyboard: it closes, the letter is in the field.
  QTest::keyClick(&window, Qt::Key_A, Qt::NoModifier);
  CHECK_FALSE(scene.entry->isOpen());
  CHECK(scene.entry->keyboardTyping());
  CHECK(scene.text("nameField") == QStringLiteral("a"));
  // Its arrows and Enter are a keyboard's again: no on-screen keyboard.
  QTest::keyClick(&window, Qt::Key_Up);
  CHECK(focusName(window) == QStringLiteral("before"));
  QTest::keyClick(&window, Qt::Key_Down);
  CHECK(window.activeFocusItem() == scene.input("nameField"));
  CHECK_FALSE(scene.entry->isOpen());
  QTest::keyClick(&window, Qt::Key_Return);
  CHECK(window.activeFocusItem() == scene.input("secretField"));
  CHECK_FALSE(scene.entry->isOpen());

  // A remote that sends only arrows and Enter (no text) is not typing.
  scene.entry->setActive(false);
  scene.entry->setActive(true);
  QTest::keyClick(&window, Qt::Key_Up);
  CHECK(window.activeFocusItem() == scene.input("nameField"));
  CHECK(scene.entry->isOpen());
  QTest::keyClick(&window, Qt::Key_Escape);
  CHECK_FALSE(scene.entry->isOpen());

  // Typed again, then the gamepad (keys the application sends): the
  // on-screen keyboard is back.
  QTest::keyClick(&window, Qt::Key_B);
  CHECK(scene.entry->keyboardTyping());
  tap(window, Qt::Key_Up);
  CHECK_FALSE(scene.entry->keyboardTyping());
  tap(window, Qt::Key_Down);
  CHECK(window.activeFocusItem() == scene.input("nameField"));
  CHECK(scene.entry->isOpen());
}

TEST_CASE("Outside TV mode a field is an ordinary field") {
  KeyboardScene scene;
  QQuickWindow& window = scene.window;
  scene.setTvMode(false);
  pump(100);
  SpatialNavigation::focusItem(scene.item(QStringLiteral("before")));
  tap(window, Qt::Key_Down);
  CHECK(window.activeFocusItem() == scene.input("nameField"));
  CHECK_FALSE(scene.entry->isOpen());
  tap(window, Qt::Key_Return);
  CHECK(window.activeFocusItem() == scene.input("secretField"));
  CHECK_FALSE(scene.entry->isOpen());
  scene.entry->show();
  CHECK_FALSE(scene.entry->isOpen());
}

TEST_CASE("Text and navigation from the phone go through the input router, keyboard open or not") {
  KeyboardScene scene;
  QQuickWindow& window = scene.window;
  ui::InputRouter router;
  router.setWindow(&window);

  SpatialNavigation::focusItem(scene.item(QStringLiteral("before")));
  // No field focused: nothing typed.
  CHECK_FALSE(router.sendText(QStringLiteral("dune")));
  router.route(platform::InputAction::Down, false);
  REQUIRE(window.activeFocusItem() == scene.input("nameField"));
  CHECK(scene.entry->isOpen());
  CHECK(router.sendText(QStringLiteral("dune")));
  CHECK(scene.text("nameField") == QStringLiteral("dune"));
  CHECK(scene.entry->isOpen());
  router.route(platform::InputAction::Back, false);
  CHECK_FALSE(scene.entry->isOpen());
  CHECK(router.sendText(QString::fromUtf8(" дюна")));
  CHECK(scene.text("nameField") == QString::fromUtf8("dune дюна"));
  // A read-only field takes nothing.
  scene.input("nameField")->setProperty("readOnly", true);
  CHECK_FALSE(router.sendText(QStringLiteral("x")));
  CHECK(scene.text("nameField") == QString::fromUtf8("dune дюна"));
}

TEST_CASE("Without TV mode a gamepad opens the on-screen keyboard; a physical keyboard or a click hands back") {
  KeyboardScene scene;
  QQuickWindow& window = scene.window;
  scene.setTvMode(false);
  ui::InputMode* mode = ui::InputMode::instance();
  mode->setGamepad(false);
  pump(100);
  window.requestActivate();
  REQUIRE(QTest::qWaitForWindowActive(&window));
  ui::InputRouter router;
  router.setWindow(&window);

  // A keyboard reaches the field: an ordinary field.
  SpatialNavigation::focusItem(scene.item(QStringLiteral("before")));
  QTest::keyClick(&window, Qt::Key_Down);
  REQUIRE(window.activeFocusItem() == scene.input("nameField"));
  CHECK_FALSE(mode->gamepad());
  CHECK_FALSE(scene.entry->isActive());
  CHECK_FALSE(scene.entry->isOpen());

  // The gamepad: Up and Down again open the keyboard on the field; its
  // keys are the desktop's size, not TV mode's.
  router.route(platform::InputAction::Up, false);
  CHECK(mode->gamepad());
  CHECK(scene.entry->isActive());
  router.route(platform::InputAction::Down, false);
  REQUIRE(window.activeFocusItem() == scene.input("nameField"));
  REQUIRE(scene.entry->isOpen());
  REQUIRE(scene.keyboard() != nullptr);
  CHECK(scene.keyboard()->isVisible());
  QQuickItem* q = scene.item(QStringLiteral("key_q"));
  REQUIRE(q != nullptr);
  CHECK(q->height() < 64);
  // Right twice and Select: "e"; Done (Enter) goes on to the secret field.
  CHECK(scene.highlighted() == QStringLiteral("q"));
  router.route(platform::InputAction::Right, false);
  router.route(platform::InputAction::Right, false);
  router.route(platform::InputAction::Select, false);
  CHECK(scene.text("nameField") == QStringLiteral("e"));
  scene.typeKey(QStringLiteral("done"));
  CHECK(window.activeFocusItem() == scene.input("secretField"));
  CHECK(scene.entry->isOpen());

  // A click on one of its keys types and keeps it open.
  QQuickItem* key = scene.item(QStringLiteral("key_x"));
  REQUIRE(key != nullptr);
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                    key->mapToScene(QPointF(key->width() / 2, key->height() / 2)).toPoint());
  CHECK_FALSE(mode->gamepad());
  CHECK(scene.text("secretField") == QStringLiteral("x"));
  CHECK(scene.entry->isOpen());
  // Back closes it; now it is a keyboard's field again.
  router.route(platform::InputAction::Back, false);
  CHECK_FALSE(scene.entry->isOpen());
  QTest::keyClick(&window, Qt::Key_Up);
  CHECK(window.activeFocusItem() == scene.input("nameField"));
  CHECK_FALSE(scene.entry->isOpen());
  CHECK_FALSE(scene.entry->isActive());

  // The gamepad again, then a physical key that types: the letter goes in,
  // the keyboard closes and stays away for the keyboard's arrows.
  router.route(platform::InputAction::Down, false);
  REQUIRE(scene.entry->isOpen());
  QTest::keyClick(&window, Qt::Key_Y, Qt::NoModifier);
  CHECK_FALSE(scene.entry->isOpen());
  CHECK_FALSE(mode->gamepad());
  CHECK(scene.text("secretField") == QStringLiteral("xy"));
  QTest::keyClick(&window, Qt::Key_Up);
  QTest::keyClick(&window, Qt::Key_Down);
  CHECK(window.activeFocusItem() == scene.input("secretField"));
  CHECK_FALSE(scene.entry->isOpen());
}

TEST_CASE("\"Show all\" after a row's last card: Right reaches it, Select opens, Left goes back") {
  for (const bool tv : {false, true}) {
    CAPTURE(tv);
    Scene scene(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Controls
Item {
  SpatialNavigation.enabled: true
  property int shown: 0
  function setTvMode(on) { Theme.tvMode = on }
  Component.onDestruction: Theme.tvMode = false
  CardRow {
    id: row
    objectName: "row"
    width: 700
    title: "Latest"
    canShowAll: true
    cardHeight: 150
    focus: true
    model: 8
    onShowAllClicked: parent.shown++
    delegate: PosterCard {
      required property int index
      objectName: "c" + index
      title: "Card " + index
      cardWidth: 90
      shape: "thumb"
    }
  }
}
)",
                QSize(800, 400));
    scene.setTvMode(tv);
    pump(30);
    QQuickWindow& window = scene.window;
    SpatialNavigation::focusItem(scene.item(QStringLiteral("c0")));
    for (int i = 0; i < 7; ++i) tap(window, Qt::Key_Right);
    REQUIRE(focusName(window) == QStringLiteral("c7"));
    tap(window, Qt::Key_Right);
    QQuickItem* tile = window.activeFocusItem();
    REQUIRE(tile != nullptr);
    CHECK(tile->objectName() == QStringLiteral("cardRowShowAllTile"));
    CHECK(tile->property("visualFocus").toBool());
    pump(400);
    // Scrolled into view.
    const QRectF shown = tile->mapRectToScene(QRectF(0, 0, tile->width(), tile->height()));
    CHECK(shown.right() <= 700 + 1);
    // Right stays; Select opens the list; Left goes back to the last card.
    tap(window, Qt::Key_Right);
    CHECK(window.activeFocusItem() == tile);
    tap(window, Qt::Key_Return);
    CHECK(scene.root->property("shown").toInt() == 1);
    tap(window, Qt::Key_Left);
    CHECK(focusName(window) == QStringLiteral("c7"));
    tap(window, Qt::Key_Left);
    CHECK(focusName(window) == QStringLiteral("c6"));
  }
}
