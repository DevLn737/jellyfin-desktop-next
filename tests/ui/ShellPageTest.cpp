#include "../support/DoctestQt.h"

#include <jfd/ui/CommandRegistry.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/ThemePalette.h>

#include <QColor>
#include <QQmlComponent>
#include <QJSValue>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)

// The shell's connection points for the other packages: the mini-player
// strip, card shapes and the palette.

using namespace jfd;

namespace {

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  std::unique_ptr<QObject> create(QQmlEngine& engine, const QByteArray& source) {
    QQmlComponent component(&engine);
    component.setData(source, QUrl(QStringLiteral("qrc:/test.qml")));
    std::unique_ptr<QObject> object(component.create());
    REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
    return object;
  }

} // namespace

TEST_CASE("The mini player strip shows under the pages wherever the shell shows its sidebar") {
  QQmlEngine engine;
  auto* navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
  REQUIRE(navigator != nullptr);
  navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
  navigator->reset(QStringLiteral("home"));

  QQuickWindow window;
  window.resize(1000, 700);
  auto object = create(engine, "import QtQuick\nimport Jfd.Ui.Shell\nAppShell { width: 1000; height: 700 }");
  auto* shell = qobject_cast<QQuickItem*>(object.get());
  shell->setParentItem(window.contentItem());
  shell->setSize(QSizeF(1000, 700));
  window.show();

  QQuickItem* slot = find(shell, "miniPlayerSlot");
  QQuickItem* pages = find(shell, "pageStack");
  REQUIRE(slot != nullptr);
  REQUIRE(pages != nullptr);
  // Nothing in it: no strip, the pages use the whole height.
  CHECK_FALSE(slot->isVisible());
  CHECK(pages->height() == doctest::Approx(700.0));

  auto bar = create(engine, "import QtQuick\nRectangle { width: 400; height: 64 }");
  auto* barItem = qobject_cast<QQuickItem*>(bar.get());
  barItem->setParentItem(slot);
  CHECK(slot->isVisible());
  CHECK(slot->height() == doctest::Approx(64.0));
  CHECK(pages->height() == doctest::Approx(636.0));
  CHECK(slot->y() + slot->height() == doctest::Approx(700.0));

  // The player and the sign-in page have no shell chrome: no strip.
  navigator->navigate(QStringLiteral("player"));
  CHECK_FALSE(slot->isVisible());
  CHECK(pages->height() == doctest::Approx(700.0));
  navigator->reset(QStringLiteral("home"));
  CHECK(slot->isVisible());

  // The sidebar lists the registered routes.
  ui::RouteSpec music;
  music.name = QStringLiteral("music");
  music.hasSidebarEntry = true;
  music.sidebar = {QStringLiteral("library"), QStringLiteral("Music"), nullptr, 400};
  navigator->registerRoute(music);
  CHECK(find(shell, "nav_music") != nullptr);
  navigator->setRouteAvailable(QStringLiteral("music"), false);
  // The Repeater removes its delegate once the model change is processed.
  QCoreApplication::processEvents();
  CHECK(find(shell, "nav_music") == nullptr);
  CHECK(find(shell, "nav_settings") != nullptr);
}

TEST_CASE("Card shapes: poster, thumb, square and circle") {
  QQmlEngine engine;
  auto object = create(engine, R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
Item {
  property var poster: Theme.cardAspectFor("poster")
  property var thumb: Theme.cardAspectFor("thumb")
  property var square: Theme.cardAspectFor("square")
  property var circle: Theme.cardAspectFor("circle")
  property var circleWidth: Theme.cardWidthFor("circle")
  PosterCard { objectName: "card"; shape: "circle"; title: "Artist" }
}
)");
  CHECK(object->property("poster").toDouble() == doctest::Approx(2.0 / 3.0));
  CHECK(object->property("thumb").toDouble() == doctest::Approx(16.0 / 9.0));
  CHECK(object->property("square").toDouble() == doctest::Approx(1.0));
  CHECK(object->property("circle").toDouble() == doctest::Approx(1.0));
  QQuickItem* card = find(qobject_cast<QQuickItem*>(object.get()), "card");
  REQUIRE(card != nullptr);
  CHECK(card->property("imageAspect").toDouble() == doctest::Approx(1.0));
  CHECK(card->property("cardWidth").toInt() == object->property("circleWidth").toInt());
}

TEST_CASE("The theme's colours come from a palette; the overlay's do not change") {
  QQmlEngine engine;
  auto object = create(engine, R"(
import QtQuick
import Jfd.Ui.Theme
QtObject {
  property color text: Theme.text
  property color overlayText: Overlay.text
  property color accent: Theme.accent
  function useLight() {
    Theme.colors = Object.assign({}, Palettes.dark, { text: "#111111", accent: "#cc3300" })
  }
}
)");
  CHECK(object->property("text").value<QColor>() == QColor(QStringLiteral("#f2f2f2")));
  CHECK(object->property("accent").value<QColor>() == QColor(QStringLiteral("#00a4dc")));
  QMetaObject::invokeMethod(object.get(), "useLight");
  CHECK(object->property("text").value<QColor>() == QColor(QStringLiteral("#111111")));
  CHECK(object->property("accent").value<QColor>() == QColor(QStringLiteral("#cc3300")));
  // The player's colours stay as they were.
  CHECK(object->property("overlayText").value<QColor>() == QColor(QStringLiteral("#f2f2f2")));
}

TEST_CASE("The palettes in QML are the ones C++ makes") {
  QQmlEngine engine;
  auto object = create(engine, R"(
import QtQuick
import Jfd.Ui.Theme
QtObject {
  property var dark: Palettes.dark
  property var light: Palettes.light
}
)");
  const auto same = [](const QVariantMap& made, const QVariant& fromQml) {
    QVariant value = fromQml;
    if (value.metaType() == QMetaType::fromType<QJSValue>()) value = value.value<QJSValue>().toVariant();
    const QVariantMap qml = value.toMap();
    CHECK(qml.size() == made.size());
    for (auto it = made.cbegin(); it != made.cend(); ++it) {
      CHECK_MESSAGE(QColor(qml.value(it.key()).toString()) == QColor(it.value().toString()), it.key().toStdString());
    }
  };
  same(ui::darkPalette(), object->property("dark"));
  same(ui::lightPalette(), object->property("light"));
}

TEST_CASE("Controls in a colour scope keep that palette when the theme changes") {
  QQmlEngine engine;
  auto object = create(engine, R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
Item {
  property color outside: outsideHeading.color
  property color inside: insideHeading.color
  function useLight() { Theme.colors = Palettes.light }
  Heading { id: outsideHeading; text: "Page" }
  Item {
    readonly property var colorScope: Overlay.colors
    Item { Heading { id: insideHeading; text: "On the video" } }
  }
}
)");
  CHECK(object->property("outside").value<QColor>() == QColor(QStringLiteral("#f2f2f2")));
  CHECK(object->property("inside").value<QColor>() == QColor(QStringLiteral("#f2f2f2")));
  QMetaObject::invokeMethod(object.get(), "useLight");
  CHECK(object->property("outside").value<QColor>() == QColor(QStringLiteral("#16181c")));
  CHECK(object->property("inside").value<QColor>() == QColor(QStringLiteral("#f2f2f2")));
}

#include <QElapsedTimer>
#include <QFile>

namespace {
  // Resident memory in bytes (Linux; elsewhere 0, and the growth check is skipped).
  qint64 residentBytes() {
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) return 0;
    const QList<QByteArray> fields = statm.readAll().split(' ');
    return fields.size() > 1 ? fields.at(1).toLongLong() * 4096 : 0;
  }
} // namespace

TEST_CASE("Switching the theme with a thousand cards on screen is quick and does not grow memory") {
  QQmlEngine engine;
  QQuickWindow window;
  window.resize(1280, 720);
  auto object = create(engine, R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
Item {
  width: 1280; height: 720
  function useLight() { Theme.colors = Palettes.light }
  function useDark() { Theme.colors = Palettes.dark }
  CardGrid {
    anchors.fill: parent
    model: 1000
    delegate: PosterCard { title: "Title " + index; subtitle: "2024" }
  }
}
)");
  auto* root = qobject_cast<QQuickItem*>(object.get());
  root->setParentItem(window.contentItem());
  window.show();
  QCoreApplication::processEvents();
  window.grabWindow();

  // The first switches allocate caches that stay; the 20 that count follow.
  for (int i = 0; i < 4; ++i) {
    QMetaObject::invokeMethod(object.get(), i % 2 == 0 ? "useLight" : "useDark");
    QCoreApplication::processEvents();
    window.grabWindow();
  }
  const qint64 before = residentBytes();
  qint64 slowest = 0;
  for (int i = 0; i < 20; ++i) {
    QElapsedTimer timer;
    timer.start();
    QMetaObject::invokeMethod(object.get(), i % 2 == 0 ? "useLight" : "useDark");
    QCoreApplication::processEvents();
    window.grabWindow();
    slowest = std::max(slowest, timer.elapsed());
  }
  const qint64 growth = residentBytes() - before;
  MESSAGE("slowest switch ", slowest, " ms, memory growth ", growth / 1024, " KiB");
  CHECK(slowest <= 100);
  if (before > 0) CHECK(growth <= 5 * 1024 * 1024);
}
