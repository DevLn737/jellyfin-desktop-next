// The library screens against a real server, driven the way a user would:
// type the address, sign in, open a library from the home page, a series
// from the grid, an episode from the season, then Play and Watch together;
// search; sign out. Needs JFD_TEST_SERVER_URL, JFD_TEST_USER and
// JFD_TEST_PASSWORD (scripts/test-server/stock-server.sh up).
//
// JFD_UI_SCREENSHOTS=<dir> saves a screenshot of each screen.

#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/platform/CredentialStore.h>
#include <jfd/ui/LibraryScreens.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_LibraryPlugin)

using namespace jfd;
using jfd::test::waitFor;

namespace {

  QString env(const char* name) {
    return qEnvironmentVariable(name);
  }

  constexpr int Timeout = 20000;

  class Screens {
  public:
    Screens() {
      ui::LibraryScreens::Config config;
      config.identity.version = QStringLiteral("0.0.1");
      config.identity.device = QStringLiteral("ui-test");
      config.identity.deviceId = QStringLiteral("jfd-ui-screens");
      config.serversFile = m_dir.filePath(QStringLiteral("servers.ini"));
      config.imageDirectory = m_dir.filePath(QStringLiteral("images"));
      config.tokens = std::make_unique<platform::MemoryCredentialStore>();
      screens = std::make_unique<ui::LibraryScreens>(std::move(config));

      navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
      REQUIRE(navigator != nullptr);
      navigator->setPlaceholderPage(
          QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
      REQUIRE(screens->install(engine, *navigator));
      screens->ui()->setPlayHandler(
          [this](const QString& id, qint64 ticks) { played.append({id, ticks}); });
      screens->ui()->setWatchTogetherHandler(
          [this](const QString& id, const QString& name, qint64 ticks) {
            together.append({id, ticks});
            togetherName = name;
          });
      navigator->reset(QStringLiteral("login"));

      engine.loadData(R"(
        import QtQuick
        import Jfd.Ui.Shell
        Window {
            width: 1280
            height: 800
            visible: true
            AppShell { objectName: "shell"; anchors.fill: parent }
        })");
      REQUIRE_FALSE(engine.rootObjects().isEmpty());
      window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
      REQUIRE(window != nullptr);
    }

    QQuickItem* page() const {
      auto* stack = window->findChild<QQuickItem*>(QStringLiteral("pageStack"));
      return stack == nullptr ? nullptr : stack->property("currentItem").value<QQuickItem*>();
    }

    // An object on the current page.
    QObject* find(const char* name) const {
      QQuickItem* current = page();
      return current == nullptr ? nullptr : current->findChild<QObject*>(QString::fromLatin1(name));
    }

    bool showing(const QString& route) const {
      QQuickItem* current = page();
      return current != nullptr && current->property("route").toString() == route &&
             navigator->currentRoute() == route;
    }

    bool waitShowing(const QString& route) const {
      return waitFor([&] { return showing(route); }, Timeout);
    }

    bool waitCount(const char* name, int atLeast) const {
      return waitFor(
          [&] {
            QObject* object = find(name);
            return object != nullptr && object->property("count").toInt() >= atLeast;
          },
          Timeout);
    }

    // A delegate of a CardRow or CardGrid.
    QQuickItem* card(const char* view, int index) const {
      QObject* owner = find(view);
      if (owner == nullptr) return nullptr;
      auto* list = owner->property("view").value<QObject*>();
      QQuickItem* item = nullptr;
      QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item),
                                Q_ARG(int, index));
      return item;
    }

    static void click(QObject* button) {
      REQUIRE(button != nullptr);
      REQUIRE(button->property("enabled").toBool());
      QMetaObject::invokeMethod(button, "clicked");
    }

    void screenshot(const QString& name) const {
      const QString dir = env("JFD_UI_SCREENSHOTS");
      if (dir.isEmpty()) return;
      // Let images arrive and animations settle.
      waitFor([] { return false; }, 1500);
      QDir().mkpath(dir);
      window->grabWindow().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    QTemporaryDir m_dir;
    QQmlApplicationEngine engine;
    std::unique_ptr<ui::LibraryScreens> screens;
    ui::Navigator* navigator = nullptr;
    QQuickWindow* window = nullptr;
    QList<QPair<QString, qint64>> played;
    QList<QPair<QString, qint64>> together;
    QString togetherName;
  };

} // namespace

TEST_CASE("Sign in, open a series from the library, an episode from its season, and play it") {
  REQUIRE_MESSAGE(!env("JFD_TEST_SERVER_URL").isEmpty(),
                  "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
  Screens screens;
  REQUIRE(screens.waitShowing(QStringLiteral("login")));
  screens.screenshot(QStringLiteral("login"));

  // The server's address, then the user's name and password.
  screens.find("serverAddress")->setProperty("text", env("JFD_TEST_SERVER_URL"));
  Screens::click(screens.find("connectButton"));
  REQUIRE(waitFor([&] { return screens.find("userName") != nullptr; }, Timeout));
  screens.find("userName")->setProperty("text", env("JFD_TEST_USER"));
  screens.find("password")->setProperty("text", env("JFD_TEST_PASSWORD"));
  CHECK(screens.find("quickConnectButton")->property("visible").toBool());
  screens.screenshot(QStringLiteral("login-credentials"));
  Screens::click(screens.find("signInButton"));
  REQUIRE(screens.waitShowing(QStringLiteral("home")));

  // Home: the libraries, then rows of cards.
  REQUIRE(screens.waitCount("librariesRow", 2));
  REQUIRE(screens.waitCount("homeRows", 1));
  CHECK(screens.find("homeError")->property("text").toString().isEmpty());
  screens.screenshot(QStringLiteral("home"));

  // The shows library from its card.
  const library::ItemListModel* libraries = screens.screens->home().libraries();
  int shows = -1;
  for (int row = 0; row < libraries->count(); ++row)
    if (libraries->entry(row)->item.collectionType == QLatin1String("tvshows")) shows = row;
  REQUIRE(shows >= 0);
  const QString showsId = libraries->entry(shows)->item.id;
  Screens::click(screens.card("librariesRow", shows));
  REQUIRE(screens.waitShowing(QStringLiteral("library/") + showsId));

  // The grid, then the first series.
  REQUIRE(screens.waitCount("libraryGrid", 1));
  QQuickItem* seriesCard = nullptr;
  REQUIRE(waitFor(
      [&] {
        seriesCard = screens.card("libraryGrid", 0);
        return seriesCard != nullptr && !seriesCard->property("itemId").toString().isEmpty();
      },
      Timeout));
  const QString seriesId = seriesCard->property("itemId").toString();
  screens.screenshot(QStringLiteral("library"));
  Screens::click(seriesCard);
  REQUIRE(screens.waitShowing(QStringLiteral("item/") + seriesId));

  // The series page: one season, its two episodes, and what Play starts.
  REQUIRE(waitFor([&] { return !screens.find("itemName")->property("text").toString().isEmpty(); },
                  Timeout));
  REQUIRE(screens.waitCount("seasonTabs", 1));
  REQUIRE(screens.waitCount("episodes", 2));
  CHECK(screens.find("playButton")->property("visible").toBool());
  CHECK_FALSE(screens.find("playTarget")->property("text").toString().isEmpty());
  screens.screenshot(QStringLiteral("series"));

  // The second episode's page.
  QQuickItem* episodeRow = nullptr;
  QMetaObject::invokeMethod(screens.find("episodes"), "itemAt",
                            Q_RETURN_ARG(QQuickItem*, episodeRow), Q_ARG(int, 1));
  REQUIRE(episodeRow != nullptr);
  Screens::click(episodeRow);
  REQUIRE(waitFor(
      [&] {
        return screens.navigator->currentName() == QLatin1String("item") &&
               screens.navigator->currentRoute() != QStringLiteral("item/") + seriesId;
      },
      Timeout));
  const QString episodeRoute = screens.navigator->currentRoute();
  REQUIRE(screens.waitShowing(episodeRoute));
  REQUIRE(waitFor([&] { return !screens.find("itemName")->property("text").toString().isEmpty(); },
                  Timeout));
  REQUIRE(
      waitFor([&] { return screens.find("playButton")->property("visible").toBool(); }, Timeout));
  screens.screenshot(QStringLiteral("episode"));

  // Play and Watch together hand the episode and its position over.
  Screens::click(screens.find("playButton"));
  REQUIRE(screens.played.size() == 1);
  CHECK(QStringLiteral("item/") + screens.played[0].first == episodeRoute);
  CHECK(screens.played[0].second == 0);
  Screens::click(screens.find("watchTogetherButton"));
  REQUIRE(screens.together.size() == 1);
  CHECK(QStringLiteral("item/") + screens.together[0].first == episodeRoute);
  CHECK_FALSE(screens.togetherName.isEmpty());

  // Card images came through the image provider and count against the
  // video memory budget.
  CHECK(waitFor([&] { return screens.screens->textures().used() > 0; }, Timeout));

  // Search.
  REQUIRE(screens.navigator->navigate(QStringLiteral("search")));
  REQUIRE(screens.waitShowing(QStringLiteral("search")));
  screens.find("searchField")->setProperty("text", QStringLiteral("Test"));
  REQUIRE(screens.waitCount("searchResults", 1));
  screens.screenshot(QStringLiteral("search"));

  // Signing out goes back to the login page.
  screens.screens->signIn().signOut();
  REQUIRE(screens.waitShowing(QStringLiteral("login")));
}
