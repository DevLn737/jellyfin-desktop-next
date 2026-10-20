#include "../support/DoctestQt.h"

#include <jfd/ui/Navigator.h>
#include <jfd/ui/WindowPlacement.h>

#include <QSignalSpy>

using namespace jfd::ui;

TEST_CASE("Routes parse with and without arguments") {
  CHECK(Navigator::parse(QStringLiteral("home")).name == QStringLiteral("home"));
  const auto item = Navigator::parse(QStringLiteral("item/abc"));
  CHECK(item.name == QStringLiteral("item"));
  CHECK(item.argument == QStringLiteral("abc"));
  CHECK(Navigator::parse(QStringLiteral("item")).name.isEmpty());
  CHECK(Navigator::parse(QStringLiteral("home/x")).name.isEmpty());
  CHECK(Navigator::parse(QStringLiteral("item/a/b")).name.isEmpty());
  CHECK(Navigator::parse(QStringLiteral("nowhere")).name.isEmpty());
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
