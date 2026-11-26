#include "../support/DoctestQt.h"

#include <jfd/ui/CommandRegistry.h>
#include <jfd/ui/GraphicsInfo.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/WindowPlacement.h>

#include <QSignalSpy>

using namespace jfd::ui;

TEST_CASE("Routes parse with and without arguments") {
  Navigator navigator;
  CHECK(navigator.parse(QStringLiteral("home")).name == QStringLiteral("home"));
  const auto item = navigator.parse(QStringLiteral("item/abc"));
  CHECK(item.name == QStringLiteral("item"));
  CHECK(item.argument == QStringLiteral("abc"));
  CHECK(navigator.parse(QStringLiteral("item")).name.isEmpty());
  CHECK(navigator.parse(QStringLiteral("home/x")).name.isEmpty());
  CHECK(navigator.parse(QStringLiteral("item/a/b")).name.isEmpty());
  CHECK(navigator.parse(QStringLiteral("nowhere")).name.isEmpty());
}

TEST_CASE("Other modules register routes, with arguments and a sidebar entry") {
  Navigator navigator;
  const auto ids = [&] {
    QStringList routes;
    for (const QVariant& entry : navigator.sidebarEntries()) {
      routes.append(entry.toMap().value(QStringLiteral("route")).toString());
    }
    return routes;
  };
  // The built-in entries, in their order.
  CHECK(ids() == QStringList{QStringLiteral("home"), QStringLiteral("group"), QStringLiteral("search"),
                             QStringLiteral("settings"), QStringLiteral("customizeHome")});
  CHECK_FALSE(navigator.navigate(QStringLiteral("music")));

  RouteSpec music;
  music.name = QStringLiteral("music");
  music.hasSidebarEntry = true;
  music.sidebar = {QStringLiteral("music"), QStringLiteral("Music"), nullptr, 400};
  navigator.registerRoute(music);
  RouteSpec album;
  album.name = QStringLiteral("album");
  album.takesArgument = true;
  navigator.registerRoute(album);
  navigator.registerPage(QStringLiteral("album"), QUrl(QStringLiteral("qrc:/Album.qml")));

  CHECK(ids() == QStringList{QStringLiteral("home"), QStringLiteral("group"), QStringLiteral("search"),
                             QStringLiteral("music"), QStringLiteral("settings"), QStringLiteral("customizeHome")});
  const QVariantMap entry = navigator.sidebarEntries().at(3).toMap();
  CHECK(entry.value(QStringLiteral("text")).toString() == QStringLiteral("Music"));
  CHECK(entry.value(QStringLiteral("icon")).toString() == QStringLiteral("music"));

  navigator.reset(QStringLiteral("home"));
  CHECK(navigator.navigate(QStringLiteral("music")));
  CHECK(navigator.navigate(QStringLiteral("album/a1")));
  CHECK(navigator.navigate(QStringLiteral("album/a2")));
  // An argument route may stack, an argument-less one is returned to.
  CHECK(navigator.navigate(QStringLiteral("album/a1")));
  CHECK(navigator.depth() == 5);
  CHECK(navigator.navigate(QStringLiteral("music")));
  CHECK(navigator.depth() == 2);
  CHECK(navigator.pageFor(QStringLiteral("album")) == QUrl(QStringLiteral("qrc:/Album.qml")));
  CHECK_FALSE(navigator.navigate(QStringLiteral("album")));
  CHECK_FALSE(navigator.navigate(QStringLiteral("music/x")));

  // A route that is not available (music off) has no sidebar entry
  // but is still a route.
  QSignalSpy sidebar(&navigator, &Navigator::sidebarChanged);
  navigator.setRouteAvailable(QStringLiteral("music"), false);
  CHECK(sidebar.count() == 1);
  CHECK(ids() == QStringList{QStringLiteral("home"), QStringLiteral("group"), QStringLiteral("search"),
                             QStringLiteral("settings"), QStringLiteral("customizeHome")});
  navigator.setRouteAvailable(QStringLiteral("music"), false);
  CHECK(sidebar.count() == 1);
  navigator.setRouteAvailable(QStringLiteral("music"), true);
  CHECK(ids().contains(QStringLiteral("music")));

  // A route without the shell around it, like the player.
  RouteSpec bare;
  bare.name = QStringLiteral("fullscreen");
  bare.chrome = false;
  navigator.registerRoute(bare);
  navigator.navigate(QStringLiteral("fullscreen"));
  CHECK_FALSE(navigator.chromeVisible());
}

TEST_CASE("A closed route is refused from anywhere and leaves the stack (SyncPlayAccess None)") {
  Navigator navigator;
  REQUIRE(navigator.reset(QStringLiteral("home")));
  REQUIRE(navigator.navigate(QStringLiteral("group")));
  QSignalSpy sidebar(&navigator, &Navigator::sidebarChanged);
  QSignalSpy current(&navigator, &Navigator::currentChanged);

  // Closing it while open goes home and drops the sidebar entry (and with
  // it Ctrl+K's "Open Watch together", which follows the sidebar).
  navigator.setRouteClosed(QStringLiteral("group"), true);
  CHECK(sidebar.count() == 1);
  CHECK(current.count() >= 1);
  CHECK(navigator.currentRoute() == QStringLiteral("home"));
  CHECK_FALSE(navigator.routeAvailable(QStringLiteral("group")));
  for (const QVariant& entry : navigator.sidebarEntries())
    CHECK(entry.toMap().value(QStringLiteral("route")).toString() != QStringLiteral("group"));

  // Every way in: navigate (sidebar, palette, a command from another start,
  // the phone's Play in a group), replace and reset.
  CHECK_FALSE(navigator.navigate(QStringLiteral("group")));
  CHECK_FALSE(navigator.replace(QStringLiteral("group")));
  CHECK_FALSE(navigator.reset(QStringLiteral("group")));
  CHECK(navigator.currentRoute() == QStringLiteral("home"));
  // Still a route: it opens again once allowed.
  CHECK(navigator.hasRoute(QStringLiteral("group")));
  navigator.setRouteClosed(QStringLiteral("group"), false);
  CHECK(navigator.routeAvailable(QStringLiteral("group")));
  CHECK(navigator.navigate(QStringLiteral("group")));
}

TEST_CASE("Commands are listed when available and run by id") {
  CommandRegistry commands;
  bool signedIn = false;
  QStringList ran;
  commands.add({QStringLiteral("navigate.home"), QStringLiteral("Go home"), {}, [&] { ran << QStringLiteral("home"); }});
  commands.add({QStringLiteral("account.signOut"), QStringLiteral("Sign out"), [&] { return signedIn; },
                [&] { ran << QStringLiteral("signOut"); }});
  // Without a way to run it, a command is not added.
  commands.add({QStringLiteral("broken"), QStringLiteral("Broken"), {}, {}});
  CHECK_FALSE(commands.contains(QStringLiteral("broken")));

  CHECK(commands.commands().size() == 1);
  CHECK_FALSE(commands.run(QStringLiteral("account.signOut")));
  signedIn = true;
  CHECK(commands.commands().size() == 2);
  CHECK(commands.commands(QStringLiteral("SIGN")).size() == 1);
  CHECK(commands.commands(QStringLiteral("SIGN")).at(0).toMap().value(QStringLiteral("id")).toString() ==
        QStringLiteral("account.signOut"));
  CHECK(commands.run(QStringLiteral("account.signOut")));
  CHECK_FALSE(commands.run(QStringLiteral("nowhere")));
  CHECK(ran == QStringList{QStringLiteral("signOut")});

  // The same id replaces the command.
  commands.add({QStringLiteral("navigate.home"), QStringLiteral("Home screen"), {}, [&] { ran << QStringLiteral("again"); }});
  CHECK(commands.commands().size() == 2);
  CHECK(commands.commands().at(0).toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Home screen"));
  commands.run(QStringLiteral("navigate.home"));
  CHECK(ran.last() == QStringLiteral("again"));

  QSignalSpy changes(&commands, &CommandRegistry::changed);
  commands.remove(QStringLiteral("navigate.home"));
  commands.remove(QStringLiteral("navigate.home"));
  CHECK(changes.count() == 1);
}

TEST_CASE("Navigation entries say where they show: top bar, account menu or library tab") {
  Navigator navigator;
  const auto placements = [&] {
    QStringList listed;
    for (const QVariant& entry : navigator.navEntries()) {
      const QVariantMap map = entry.toMap();
      listed.append(map.value(QStringLiteral("route")).toString() + QLatin1Char(':') +
                    map.value(QStringLiteral("placement")).toString());
    }
    return listed;
  };
  CHECK(placements() == QStringList{QStringLiteral("home:primary"), QStringLiteral("library:primary"),
                                    QStringLiteral("group:primary"), QStringLiteral("search:primary"),
                                    QStringLiteral("settings:account"), QStringLiteral("customizeHome:account")});
  RouteSpec tv;
  tv.name = QStringLiteral("livetv");
  tv.hasSidebarEntry = true;
  tv.sidebar.text = QStringLiteral("Live TV");
  tv.sidebar.order = 160;
  tv.sidebar.placement = RouteSpec::Placement::LibraryTab;
  navigator.registerRoute(tv);
  CHECK(placements().at(2) == QStringLiteral("livetv:libraryTab"));
  // The sidebar lists the libraries themselves, not a Library entry.
  for (const QVariant& entry : navigator.sidebarEntries())
    CHECK(entry.toMap().value(QStringLiteral("route")).toString() != QStringLiteral("library"));
}

TEST_CASE("Library takes an optional argument and its tabs replace the history entry") {
  Navigator navigator;
  QSignalSpy changes(&navigator, &Navigator::stackChanged);
  CHECK(navigator.parse(QStringLiteral("library")).name == QStringLiteral("library"));
  CHECK(navigator.parse(QStringLiteral("library/movies")).argument == QStringLiteral("movies"));
  CHECK(navigator.parse(QStringLiteral("library/")).name.isEmpty());
  CHECK(navigator.parse(QStringLiteral("library/a/b")).name.isEmpty());

  REQUIRE(navigator.reset(QStringLiteral("home")));
  CHECK(navigator.navigate(QStringLiteral("library")));
  CHECK(navigator.stack() == QStringList{QStringLiteral("home"), QStringLiteral("library")});
  // A tab: the same page's entry, no new history.
  changes.clear();
  CHECK(navigator.navigate(QStringLiteral("library/shows")));
  CHECK(navigator.stack() == QStringList{QStringLiteral("home"), QStringLiteral("library/shows")});
  REQUIRE(changes.count() == 1);
  CHECK(changes.last().at(0).value<Navigator::Operation>() == Navigator::Operation::Replace);
  // From an item opened in the library, Library goes back to the grid and
  // keeps its tab; an old "library/<id>" address switches it.
  CHECK(navigator.navigate(QStringLiteral("item/1")));
  CHECK(navigator.navigate(QStringLiteral("library")));
  CHECK(navigator.stack() == QStringList{QStringLiteral("home"), QStringLiteral("library/shows")});
  CHECK(navigator.navigate(QStringLiteral("item/1")));
  CHECK(navigator.navigate(QStringLiteral("library/movies")));
  CHECK(navigator.stack() == QStringList{QStringLiteral("home"), QStringLiteral("library/movies")});
  CHECK(navigator.back());
  CHECK(navigator.currentRoute() == QStringLiteral("home"));
}

TEST_CASE("The navigator keeps a stack and tells the view what to do") {
  Navigator navigator;
  navigator.setPlaceholderPage(QUrl(QStringLiteral("qrc:/Placeholder.qml")));
  navigator.registerPage(QStringLiteral("settings"), QUrl(QStringLiteral("qrc:/Settings.qml")));
  QSignalSpy changes(&navigator, &Navigator::stackChanged);

  CHECK(navigator.reset(QStringLiteral("login")));
  CHECK_FALSE(navigator.chromeVisible());
  CHECK(navigator.navigate(QStringLiteral("home")));
  CHECK(navigator.depth() == 1);
  CHECK(navigator.chromeVisible());
  CHECK(navigator.navigate(QStringLiteral("library/movies")));
  CHECK(navigator.navigate(QStringLiteral("item/1")));
  CHECK(navigator.navigate(QStringLiteral("settings")));
  CHECK(navigator.stack() == QStringList{QStringLiteral("home"), QStringLiteral("library/movies"),
                                         QStringLiteral("item/1"), QStringLiteral("settings")});
  const QList<QVariant> last = changes.last();
  CHECK(last.at(0).value<Navigator::Operation>() == Navigator::Operation::Push);
  CHECK(last.at(1).toUrl() == QUrl(QStringLiteral("qrc:/Settings.qml")));
  CHECK(last.at(2).toMap().value(QStringLiteral("route")).toString() == QStringLiteral("settings"));

  // Same route again: nothing happens.
  const auto before = changes.count();
  CHECK(navigator.navigate(QStringLiteral("settings")));
  CHECK(changes.count() == before);

  // Unknown routes are refused.
  CHECK_FALSE(navigator.navigate(QStringLiteral("nowhere")));

  // An earlier non-item page is returned to, not stacked twice.
  CHECK(navigator.navigate(QStringLiteral("search")));
  CHECK(navigator.navigate(QStringLiteral("settings")));
  CHECK(navigator.currentRoute() == QStringLiteral("settings"));
  CHECK(navigator.depth() == 4);

  CHECK(navigator.back());
  CHECK(navigator.currentRoute() == QStringLiteral("item/1"));
  CHECK(changes.last().at(0).value<Navigator::Operation>() == Navigator::Operation::Pop);
  CHECK(changes.last().at(1).toUrl() == QUrl(QStringLiteral("qrc:/Placeholder.qml")));
  CHECK(navigator.replace(QStringLiteral("item/2")));
  CHECK(navigator.stack().last() == QStringLiteral("item/2"));

  // "home" is a root: going there clears the stack.
  CHECK(navigator.navigate(QStringLiteral("home")));
  CHECK(navigator.depth() == 1);
  CHECK_FALSE(navigator.back());

  navigator.navigate(QStringLiteral("player"));
  CHECK_FALSE(navigator.chromeVisible());
  changes.clear();
  navigator.replay();
  REQUIRE(changes.count() == 2);
  CHECK(changes.at(0).at(0).value<Navigator::Operation>() == Navigator::Operation::Reset);
  CHECK(changes.at(1).at(0).value<Navigator::Operation>() == Navigator::Operation::Push);
}

TEST_CASE("A long walk through item pages keeps at most MaxDepth pages, home first and the newest") {
  Navigator navigator;
  QSignalSpy changes(&navigator, &Navigator::stackChanged);
  CHECK(navigator.reset(QStringLiteral("home")));
  for (int i = 1; i < Navigator::MaxDepth; ++i) CHECK(navigator.navigate(QStringLiteral("item/%1").arg(i)));
  CHECK(navigator.depth() == Navigator::MaxDepth);
  CHECK(changes.last().at(0).value<Navigator::Operation>() == Navigator::Operation::Push);

  // One more: the oldest items go, home and the newest stay.
  CHECK(navigator.navigate(QStringLiteral("item/%1").arg(Navigator::MaxDepth)));
  CHECK(navigator.depth() == Navigator::TrimmedDepth);
  CHECK(changes.last().at(0).value<Navigator::Operation>() == Navigator::Operation::Rebuild);
  const QStringList stack = navigator.stack();
  CHECK(stack.first() == QStringLiteral("home"));
  CHECK(stack.last() == QStringLiteral("item/%1").arg(Navigator::MaxDepth));
  CHECK(stack.at(1) == QStringLiteral("item/%1").arg(Navigator::MaxDepth - Navigator::TrimmedDepth + 2));
  // The view gets every page with its properties, bottom first.
  const QVariantList pages = navigator.pages();
  REQUIRE(pages.size() == 2 * Navigator::TrimmedDepth);
  CHECK(pages.at(1).toMap().value(QStringLiteral("route")).toString() == QStringLiteral("home"));
  CHECK(pages.last().toMap().value(QStringLiteral("route")).toString() == stack.last());

  // Back walks the kept pages down to home.
  for (int i = 1; i < Navigator::TrimmedDepth; ++i) CHECK(navigator.back());
  CHECK(navigator.currentRoute() == QStringLiteral("home"));
  CHECK_FALSE(navigator.back());

  // Many more pages never grow it past the limit.
  for (int i = 0; i < 200; ++i) {
    CHECK(navigator.navigate(QStringLiteral("item/x%1").arg(i)));
    CHECK(navigator.depth() <= Navigator::MaxDepth);
  }
}

TEST_CASE("Geometry round-trips through the settings text") {
  const QRect rect(-100, 20, 1280, 800);
  CHECK(decodeGeometry(encodeGeometry(rect)) == rect);
  CHECK_FALSE(decodeGeometry(QStringLiteral("1,2,3")).has_value());
  CHECK_FALSE(decodeGeometry(QStringLiteral("1,2,0,4")).has_value());
  CHECK_FALSE(decodeGeometry(QStringLiteral("a,b,c,d")).has_value());
}

TEST_CASE("The window opens where it was, or on a screen that is still there") {
  const QList<ScreenArea> screens = {
      {QStringLiteral("left"), QRect(0, 0, 1920, 1040)},
      {QStringLiteral("right"), QRect(1920, 0, 2560, 1400)},
  };
  const QSize defaultSize(1280, 800);

  // First start: default size, centred on the primary screen.
  CHECK(placeWindow(std::nullopt, {}, screens, 0, defaultSize) == QRect(320, 120, 1280, 800));

  // Saved on a connected screen: exactly where it was.
  const QRect onRight(2000, 100, 1600, 900);
  CHECK(placeWindow(onRight, QStringLiteral("right"), screens, 0, defaultSize) == onRight);

  // That screen is gone: same size, centred on the primary screen.
  const QList<ScreenArea> onlyLeft = {screens[0]};
  CHECK(placeWindow(onRight, QStringLiteral("right"), onlyLeft, 0, defaultSize) == QRect(160, 70, 1600, 900));

  // Saved off the visible area of its screen (resolution changed): centred.
  const QRect offScreen(1900, 1030, 800, 600);
  CHECK(placeWindow(offScreen, QStringLiteral("left"), onlyLeft, 0, defaultSize) == QRect(560, 220, 800, 600));

  // Larger than the screen now: shrunk to fit.
  const QRect huge(0, 0, 4000, 3000);
  CHECK(placeWindow(huge, QStringLiteral("left"), onlyLeft, 0, defaultSize).size() == QSize(1920, 1040));
}

TEST_CASE("Leaving full screen brings back the window's size and maximized state") {
  using Restore = WindowShape::Restore;
  const QRect screen(0, 0, 2560, 1440);
  const QRect normal(200, 150, 1500, 900);

  // A plain window, resized by the user, goes full screen and back.
  WindowShape shape;
  shape.start(QRect(640, 320, 1280, 800), false);
  CHECK(shape.visibilityChanged(QWindow::Windowed) == Restore::Nothing);
  shape.geometryChanged(QWindow::Windowed, normal, screen);
  CHECK(shape.visibilityChanged(QWindow::FullScreen) == Restore::Nothing);
  // The switch reports the screen-sized rectangle, sometimes still as a
  // plain window: not the window's own size.
  shape.geometryChanged(QWindow::Windowed, screen, screen);
  shape.geometryChanged(QWindow::FullScreen, screen, screen);
  CHECK(shape.visibilityChanged(QWindow::Windowed) == Restore::Geometry);
  CHECK(shape.normalGeometry() == normal);
  CHECK_FALSE(shape.maximized());

  // A maximized window goes back to maximized, keeping its normal size.
  CHECK(shape.visibilityChanged(QWindow::Maximized) == Restore::Nothing);
  shape.geometryChanged(QWindow::Maximized, QRect(0, 0, 2560, 1400), screen);
  CHECK(shape.visibilityChanged(QWindow::FullScreen) == Restore::Nothing);
  CHECK(shape.maximized());
  CHECK(shape.visibilityChanged(QWindow::Windowed) == Restore::Maximize);
  CHECK(shape.normalGeometry() == normal);
  CHECK(shape.visibilityChanged(QWindow::Maximized) == Restore::Nothing);

  // Un-maximizing by hand is not a full-screen exit: nothing to undo.
  CHECK(shape.visibilityChanged(QWindow::Windowed) == Restore::Nothing);
  CHECK_FALSE(shape.maximized());

  // Minimizing in full screen and coming back keeps the memory.
  CHECK(shape.visibilityChanged(QWindow::FullScreen) == Restore::Nothing);
  CHECK(shape.visibilityChanged(QWindow::Minimized) == Restore::Nothing);
  CHECK(shape.visibilityChanged(QWindow::FullScreen) == Restore::Nothing);
  CHECK(shape.visibilityChanged(QWindow::Windowed) == Restore::Geometry);
}

TEST_CASE("A window started in full screen leaves it into the saved shape") {
  using Restore = WindowShape::Restore;
  WindowShape maximized;
  maximized.start(QRect(100, 100, 1280, 800), true);
  CHECK(maximized.visibilityChanged(QWindow::FullScreen) == Restore::Nothing);
  CHECK(maximized.visibilityChanged(QWindow::Windowed) == Restore::Maximize);

  WindowShape plain;
  plain.start(QRect(100, 100, 1280, 800), false);
  CHECK(plain.visibilityChanged(QWindow::FullScreen) == Restore::Nothing);
  CHECK(plain.visibilityChanged(QWindow::Windowed) == Restore::Geometry);
  CHECK(plain.normalGeometry() == QRect(100, 100, 1280, 800));
}

TEST_CASE("Simplified effects turn on with integrated and software graphics") {
  using Kind = GraphicsDevice::Kind;
  const auto device = [](Kind kind, quint32 vendor, const char* name) {
    GraphicsDevice d;
    d.kind = kind;
    d.vendorId = vendor;
    d.name = QString::fromUtf8(name);
    return d;
  };
  // Vulkan says so directly.
  CHECK(prefersReducedEffects(device(Kind::Integrated, 0x1002, "AMD Radeon RX 9060 XT")));
  CHECK_FALSE(prefersReducedEffects(device(Kind::Discrete, 0x8086, "Intel(R) Iris(R) Xe Graphics")));
  CHECK(prefersReducedEffects(device(Kind::Software, 0, "")));
  // Direct3D 11 and OpenGL: by vendor and name.
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0x8086, "Intel(R) Iris(R) Xe Graphics")));
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0, "Mesa Intel(R) Xe Graphics (TGL GT2)")));
  CHECK_FALSE(prefersReducedEffects(device(Kind::Unknown, 0x8086, "Intel(R) Arc(TM) A770 Graphics")));
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0x1002, "AMD Radeon(TM) Graphics")));
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0x1002, "AMD Radeon 780M Graphics")));
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0, "AMD Radeon Graphics (radeonsi, rembrandt, LLVM 17)")));
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0x1002, "AMD Radeon(TM) Vega 8 Graphics")));
  CHECK_FALSE(prefersReducedEffects(device(Kind::Unknown, 0x1002, "AMD Radeon RX 9060 XT")));
  CHECK_FALSE(prefersReducedEffects(device(Kind::Unknown, 0, "AMD Radeon RX 9060 XT (radeonsi, gfx1200, LLVM 19)")));
  CHECK_FALSE(prefersReducedEffects(device(Kind::Unknown, 0x10de, "NVIDIA GeForce RTX 4070")));
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0, "llvmpipe (LLVM 15.0.7, 256 bits)")));
  CHECK(prefersReducedEffects(device(Kind::Unknown, 0x1414, "Microsoft Basic Render Driver")));
}
