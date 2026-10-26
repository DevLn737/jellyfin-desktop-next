#include "../support/DoctestQt.h"

#include <jfd/ui/CommandRegistry.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/ThemePalette.h>

#include <QColor>
#include <QElapsedTimer>
#include <QFile>
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

TEST_CASE("Layout at window sizes from 640x420 to 4K, in logical pixels") {
  QQmlEngine engine;
  auto* navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
  navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
  navigator->reset(QStringLiteral("home"));
  auto* theme = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme");
  REQUIRE(theme != nullptr);

  QQuickWindow window;
  auto object = create(engine, "import QtQuick\nimport Jfd.Ui.Shell\nAppShell { width: 1000; height: 700 }");
  auto* shell = qobject_cast<QQuickItem*>(object.get());
  shell->setParentItem(window.contentItem());
  window.show();

  struct Size {
    int width;
    int height;
    const char* sizeClass;
    int sidebar;
  };
  // 1366x768 at 150 % is 911x512; 1920x1080 at 200 % is 960x540; 3840x2160
  // at 100/150/200 % is 3840x2160, 2560x1440 and 1920x1080.
  const Size sizes[] = {{640, 420, "compact", 64},    {899, 600, "compact", 64},   {900, 600, "normal", 220},
                        {911, 512, "normal", 220},    {960, 540, "normal", 220},   {1024, 768, "normal", 220},
                        {1280, 720, "normal", 220},   {1600, 900, "normal", 220},  {1920, 1080, "spacious", 220},
                        {2560, 1440, "spacious", 220}, {3840, 2160, "spacious", 220}};
  QQuickItem* stack = find(shell, "pageStack");
  REQUIRE(stack != nullptr);
  for (const Size& size : sizes) {
    CAPTURE(size.width);
    window.resize(size.width, size.height);
    shell->setSize(QSizeF(size.width, size.height));
    QCoreApplication::processEvents();
    CHECK(theme->property("sizeClass").toString() == QLatin1String(size.sizeClass));
    // The sidebar is a rail in a compact window; the pages get the rest.
    CHECK(stack->x() == doctest::Approx(size.sidebar));
    CHECK(stack->x() + stack->width() == doctest::Approx(size.width));
    CHECK(stack->height() == doctest::Approx(size.height));
    // Cards are 110 to 220 px wide, and at least one fits beside the margins.
    const int poster = theme->property("posterWidth").toInt();
    CHECK(poster >= 110);
    CHECK(poster <= 220);
    const int margin = theme->property("pageMargin").toInt();
    CHECK(stack->width() - 2 * margin >= poster);
    const int thumb = theme->property("thumbWidth").toInt();
    CHECK(stack->width() - 2 * margin >= thumb);
  }
}

TEST_CASE("The menu opens the names over the pages in a compact window") {
  QQmlEngine engine;
  auto* navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
  navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
  navigator->reset(QStringLiteral("home"));
  QQuickWindow window;
  window.resize(700, 500);
  auto object = create(engine, "import QtQuick\nimport Jfd.Ui.Shell\nAppShell { width: 700; height: 500 }");
  auto* shell = qobject_cast<QQuickItem*>(object.get());
  shell->setParentItem(window.contentItem());
  window.show();
  QCoreApplication::processEvents();

  QQuickItem* sidebar = find(shell, "sidebar");
  QQuickItem* stack = find(shell, "pageStack");
  QQuickItem* menu = find(shell, "menuButton");
  REQUIRE(sidebar != nullptr);
  REQUIRE(stack != nullptr);
  REQUIRE(menu != nullptr);
  CHECK(menu->isVisible());
  CHECK(sidebar->width() == doctest::Approx(64.0));
  const qreal pageX = stack->x();

  QMetaObject::invokeMethod(menu, "clicked");
  QElapsedTimer waited;
  waited.start();
  while (sidebar->width() <= 200 && waited.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  REQUIRE(sidebar->width() > 200);
  // Over the pages, which do not move.
  CHECK(stack->x() == doctest::Approx(pageX));
  CHECK(shell->property("menuOpen").toBool());

  // Choosing a destination closes it.
  QQuickItem* search = find(shell, "nav_search");
  REQUIRE(search != nullptr);
  QMetaObject::invokeMethod(search, "clicked");
  CHECK_FALSE(shell->property("menuOpen").toBool());

  // A wide window has no menu button.
  window.resize(1200, 700);
  shell->setSize(QSizeF(1200, 700));
  QCoreApplication::processEvents();
  CHECK_FALSE(menu->isVisible());
  waited.restart();
  while (sidebar->width() < 220 && waited.elapsed() < 2000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  CHECK(sidebar->width() == doctest::Approx(220.0));
}

#include <QDir>

// JFD_UI_SCREENSHOTS=<dir>: a sample page in each theme at several window
// sizes, for the theme and size gallery in CI.
TEST_CASE("Theme and size gallery") {
  const QString dir = qEnvironmentVariable("JFD_UI_SCREENSHOTS");
  if (dir.isEmpty()) return;
  QDir().mkpath(dir);
  QQmlEngine engine;
  auto* navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
  navigator->setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
  navigator->reset(QStringLiteral("home"));
  auto* theme = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme");
  auto* overlay = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Overlay");
  QQuickWindow window;
  auto object = create(engine, R"(
import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
Item {
  id: root
  width: 1280; height: 720
  AppShell { id: shell; anchors.fill: parent }
  Rectangle {
    x: shell.stack.x; y: shell.stack.y; width: shell.stack.width; height: shell.stack.height
    color: Theme.background
    Column {
      anchors.fill: parent
      anchors.margins: Theme.pageMargin
      spacing: Theme.spacingLarge
      Heading { text: "Home" }
      Flow {
        width: parent.width
        spacing: Theme.spacing
        ActionButton { text: "Play"; primary: true; iconSource: Icons.play }
        ActionButton { text: "Watch together"; iconSource: Icons.group }
        IconButton { iconSource: Icons.search; toolTipText: "Search" }
        ToggleSwitch { checked: true }
      }
      InputField { width: Math.min(parent.width, 360); label: "Server address"; text: "https://jellyfin.example" }
      ListItem { width: Math.min(parent.width, 360); text: "Library"; subtitle: "Movies"; iconSource: Icons.library; highlighted: true }
      CardRow {
        width: parent.width
        title: "Continue watching"
        model: 12
        cardHeight: Math.round(Theme.posterWidth / Theme.posterAspect) + 52
        delegate: PosterCard { title: "Title " + index; subtitle: "2024"; progress: index % 3 ? 0.4 : 0; unplayedCount: index % 4 }
      }
    }
  }
}
)");
  auto* root = qobject_cast<QQuickItem*>(object.get());
  root->setParentItem(window.contentItem());
  window.show();

  struct Look {
    const char* name;
    bool dark;
    const char* accent;
  };
  const Look looks[] = {{"dark", true, "#00a4dc"}, {"light", false, "#00a4dc"}, {"light-purple", false, "#8e6be8"},
                        {"dark-orange", true, "#f08c2e"}};
  const QSize sizes[] = {{640, 420}, {1024, 768}, {1280, 720}, {1920, 1080}};
  for (const Look& look : looks) {
    const QVariantMap palette = ui::withAccent(look.dark ? ui::darkPalette() : ui::lightPalette(),
                                               QColor(QString::fromLatin1(look.accent)), look.dark);
    theme->setProperty("colors", palette);
    overlay->setProperty("colors", ui::withAccent(ui::darkPalette(), QColor(QString::fromLatin1(look.accent)), true));
    for (const QSize& size : sizes) {
      window.resize(size);
      root->setSize(QSizeF(size));
      QElapsedTimer settle;
      settle.start();
      while (settle.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      const QString file = QStringLiteral("%1/%2-%3x%4.png").arg(dir, QString::fromLatin1(look.name)).arg(size.width()).arg(size.height());
      CHECK(window.grabWindow().save(file));
    }
  }
}
