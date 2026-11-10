// TV mode (stage 3, package 3.3): SpatialNavigation on synthetic screens
// and on the real shell, every focusable control answering Enter, the TV
// sizes and safe area, switching TV mode on a screen of 1000 cards, and the
// gamepad check's state. Only arrows, Enter, Back and Menu: no mouse.
#include "../support/DoctestQt.h"

#include <jfd/platform/InputAction.h>
#include <jfd/ui/GamepadStatus.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/SpatialNavigation.h>

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
    PosterCard { objectName: "PosterCard"; title: "Title"; onClicked: root.answered("PosterCard") }
    ListItem { objectName: "ListItem"; text: "Entry"; onClicked: root.answered("ListItem") }
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
                              QStringLiteral("PosterCard"),   QStringLiteral("ListItem"),
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

TEST_CASE("A drop-down list keeps Up and Down for the focus in TV mode, for its value otherwise") {
  Scene scene(R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Controls
Item {
  function setTvMode(on) { Theme.tvMode = on }
  SpatialNavigation.enabled: true
  Column {
    spacing: 8
    ActionButton { objectName: "above"; text: "Above" }
    ChoiceBox {
      objectName: "choice"
      model: [{ value: "a", label: "A" }, { value: "b", label: "B" }, { value: "c", label: "C" }]
      currentIndex: 1
    }
    ActionButton { objectName: "below"; text: "Below" }
  }
}
)");
  QQuickWindow& window = scene.window;
  QQuickItem* choice = scene.item(QStringLiteral("choice"));
  REQUIRE(choice != nullptr);
  choice->forceActiveFocus(Qt::TabFocusReason);
  tap(window, Qt::Key_Down);
  CHECK(choice->property("currentIndex").toInt() == 2);
  CHECK(focusName(window) == QStringLiteral("choice"));

  scene.setTvMode(true);
  tap(window, Qt::Key_Down);
  CHECK(focusName(window) == QStringLiteral("below"));
  tap(window, Qt::Key_Up);
  CHECK(focusName(window) == QStringLiteral("choice"));
  tap(window, Qt::Key_Up);
  CHECK(focusName(window) == QStringLiteral("above"));
  CHECK(choice->property("currentIndex").toInt() == 2);
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
  CHECK(slowest <= 100);
#if defined(__SANITIZE_ADDRESS__)
  constexpr bool residentMeaningful = false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
  constexpr bool residentMeaningful = false;
#else
  constexpr bool residentMeaningful = true;
#endif
#else
  constexpr bool residentMeaningful = true;
#endif
  if (residentMeaningful && before > 0) CHECK(growth <= 5 * 1024 * 1024);
}

namespace {

  // The real shell on a test page of buttons, at 1920x1080.
  struct ShellScene {
    ShellScene() {
      REQUIRE(pages.isValid());
      const QString page = pages.filePath(QStringLiteral("TestPage.qml"));
      QFile file(page);
      REQUIRE(file.open(QIODevice::WriteOnly));
      file.write(R"(
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
)");
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

TEST_CASE("The shell in TV mode: safe area, every item reachable, Menu goes to the sidebar and back") {
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

  // From the first button: Left to the sidebar, Right back to the page.
  QQuickItem* first = scene.item(QStringLiteral("button0"));
  REQUIRE(first != nullptr);
  SpatialNavigation::focusItem(first);
  tap(window, Qt::Key_Left);
  CHECK(focusName(window).startsWith(QLatin1String("nav_")));
  tap(window, Qt::Key_Right);
  CHECK(focusName(window).startsWith(QLatin1String("button")));

  // Menu: the sidebar's entry of the current page; Menu again: the page.
  SpatialNavigation::focusItem(scene.item(QStringLiteral("button5")));
  tap(window, Qt::Key_Menu);
  CHECK(focusName(window) == QStringLiteral("nav_home"));
  tap(window, Qt::Key_Menu);
  CHECK(focusName(window).startsWith(QLatin1String("button")));

  // Every focusable item reaches every other by arrows (a scope such as
  // the sidebar passes the focus on to its entry: not a node of its own).
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
