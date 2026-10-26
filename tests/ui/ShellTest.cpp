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
  CHECK(ids() == QStringList{QStringLiteral("home"), QStringLiteral("search"), QStringLiteral("group"),
                             QStringLiteral("settings")});
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

  CHECK(ids() == QStringList{QStringLiteral("home"), QStringLiteral("search"), QStringLiteral("group"),
                             QStringLiteral("music"), QStringLiteral("settings")});
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

  // A route that is not available (no Seerr address) has no sidebar entry
  // but is still a route.
  QSignalSpy sidebar(&navigator, &Navigator::sidebarChanged);
  navigator.setRouteAvailable(QStringLiteral("music"), false);
  CHECK(sidebar.count() == 1);
  CHECK(ids() == QStringList{QStringLiteral("home"), QStringLiteral("search"), QStringLiteral("group"),
                             QStringLiteral("settings")});
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
