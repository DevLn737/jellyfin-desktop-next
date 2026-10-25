// The library screens against a real server, driven the way a user would:
// type the address, sign in, open a library from the home page, a series
// from the grid, an episode from the season, then Play and Watch together;
// search; sign out. Needs JFD_TEST_SERVER_URL, JFD_TEST_USER and
// JFD_TEST_PASSWORD (scripts/test-server/stock-server.sh up).
//
// JFD_UI_SCREENSHOTS=<dir> saves a screenshot of each screen.

#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/platform/CredentialStore.h>
#include <jfd/ui/LibraryScreens.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

namespace {
  void signInAs(Screens& screens) {
    REQUIRE(screens.waitShowing(QStringLiteral("login")));
    screens.find("serverAddress")->setProperty("text", env("JFD_TEST_SERVER_URL"));
    Screens::click(screens.find("connectButton"));
    REQUIRE(waitFor([&] { return screens.find("userName") != nullptr; }, Timeout));
    screens.find("userName")->setProperty("text", env("JFD_TEST_USER"));
    screens.find("password")->setProperty("text", env("JFD_TEST_PASSWORD"));
    Screens::click(screens.find("signInButton"));
    REQUIRE(screens.waitShowing(QStringLiteral("home")));
    REQUIRE(screens.waitCount("librariesRow", 2));
  }

  // Sets a ChoiceBox the way a click on one of its entries does.
  void choose(QObject* box, int index) {
    REQUIRE(box != nullptr);
    box->setProperty("currentIndex", index);
    QMetaObject::invokeMethod(box, "activated", Q_ARG(int, index));
  }

  // The card of `view` showing `itemId`, if it has been made.
  QQuickItem* cardFor(const Screens& screens, const char* view, const QString& itemId) {
    for (int i = 0; i < 20; ++i) {
      QQuickItem* card = screens.card(view, i);
      if (card != nullptr && card->property("itemId").toString() == itemId) return card;
    }
    return nullptr;
  }

  QObject* libraryModel(const Screens& screens) {
    QQuickItem* page = screens.page();
    REQUIRE(page != nullptr);
    return page->property("library").value<QObject*>();
  }
} // namespace

TEST_CASE("Year, several genres and collections in the library; the collection page") {
  REQUIRE_MESSAGE(!env("JFD_TEST_SERVER_URL").isEmpty(),
                  "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
  Screens screens;
  signInAs(screens);
  api::JellyfinClient& client = screens.screens->client();

  // A movie and a collection holding it, made through the API.
  QString movieId;
  api::ItemQuery query;
  query.includeItemTypes = {QStringLiteral("Movie")};
  query.limit = 1;
  bool answered = false;
  client.items(query, &client, [&](api::Result<api::ItemPage> result) {
    REQUIRE(result);
    REQUIRE(result->items.size() == 1);
    movieId = result->items[0].id;
    answered = true;
  });
  REQUIRE(waitFor([&] { return answered; }, Timeout));

  // The server saves a collection's metadata file in the background, and
  // saving it while a library scan runs can fail ("collection.xml being
  // used by another process"), leaving a collection that is not listed.
  // So wait for the scan, and if the new collection does not show up with
  // its movie, remove it and make it again.
  auto call = [&](api::JellyfinClient::Method method, const QString& path, const QUrlQuery& q) {
    bool done = false;
    api::JellyfinClient::RawResponse response;
    client.send(method, path, q, std::nullopt, &client,
                [&](api::Result<api::JellyfinClient::RawResponse> r) {
                  if (r) response = r.value();
                  done = true;
                });
    waitFor([&] { return done; }, Timeout);
    return response;
  };
  waitFor(
      [&] {
        const auto tasks =
            QJsonDocument::fromJson(
                call(api::JellyfinClient::Method::Get, QStringLiteral("ScheduledTasks"), {}).body)
                .array();
        for (const QJsonValue& task : tasks) {
          if (task.toObject().value(QStringLiteral("Key")).toString() ==
              QLatin1String("RefreshLibrary"))
            return task.toObject().value(QStringLiteral("State")).toString() ==
                   QLatin1String("Idle");
        }
        return true;
      },
      60000);

  QString collectionId;
  auto listed = [&](const QString& id) {
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("parentId"), id);
    q.addQueryItem(QStringLiteral("enableImages"), QStringLiteral("false"));
    const auto items = QJsonDocument::fromJson(
                           call(api::JellyfinClient::Method::Get, QStringLiteral("Items"), q).body)
                           .object()
                           .value(QStringLiteral("Items"))
                           .toArray();
    for (const QJsonValue& item : items)
      if (item.toObject().value(QStringLiteral("Id")).toString() == movieId) return true;
    return false;
  };
  for (int attempt = 0; attempt < 4 && collectionId.isEmpty(); ++attempt) {
    QUrlQuery create;
    create.addQueryItem(QStringLiteral("name"), QStringLiteral("Stand collection"));
    create.addQueryItem(QStringLiteral("ids"), movieId);
    const auto made =
        call(api::JellyfinClient::Method::Post, QStringLiteral("Collections"), create);
    const QString id =
        QJsonDocument::fromJson(made.body).object().value(QStringLiteral("Id")).toString();
    if (id.isEmpty()) continue;
    // Listed with its movie, and found among the collections.
    if (waitFor([&] { return listed(id); }, 8000) &&
        waitFor(
            [&] {
              QUrlQuery q;
              q.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("BoxSet"));
              q.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
              return call(api::JellyfinClient::Method::Get, QStringLiteral("Items"), q)
                  .body.contains(id.toUtf8());
            },
            8000)) {
      collectionId = id;
    } else {
      call(api::JellyfinClient::Method::Delete, QStringLiteral("Items/") + id, {});
    }
  }
  REQUIRE_FALSE(collectionId.isEmpty());
  // Whatever the checks below do, the server is left as it was found.
  auto cleanup = [&] {
    bool removed = false;
    client.sendNoContent(api::JellyfinClient::Method::Delete,
                         QStringLiteral("Items/") + collectionId, {}, std::nullopt, &client,
                         [&](api::Result<api::Empty>) { removed = true; });
    waitFor([&] { return removed; }, Timeout);
  };
  struct Guard {
    std::function<void()> run;
    ~Guard() { run(); }
  } guard{cleanup};

  // The movies library from home.
  const library::ItemListModel* libraries = screens.screens->home().libraries();
  QString moviesId;
  for (int row = 0; row < libraries->count(); ++row)
    if (libraries->entry(row)->item.collectionType == QLatin1String("movies"))
      moviesId = libraries->entry(row)->item.id;
  REQUIRE_FALSE(moviesId.isEmpty());
  REQUIRE(screens.navigator->navigate(QStringLiteral("library/") + moviesId));
  REQUIRE(screens.waitShowing(QStringLiteral("library/") + moviesId));
  REQUIRE(screens.waitCount("libraryGrid", 1));
  QObject* model = libraryModel(screens);
  REQUIRE(model != nullptr);
  CHECK(model->property("canShowCollections").toBool());
  const int movies = model->property("count").toInt();

  // Year: the library offers its years, and one year narrows the grid.
  REQUIRE(waitFor([&] { return model->property("availableYears").toList().size() > 0; }, Timeout));
  QObject* year = screens.find("libraryYear");
  REQUIRE(year != nullptr);
  CHECK(year->property("visible").toBool());
  const int yearValue = model->property("availableYears").toList().first().toInt();
  int yearIndex = -1;
  QMetaObject::invokeMethod(year, "indexOfValue", Q_RETURN_ARG(int, yearIndex),
                            Q_ARG(QVariant, yearValue));
  REQUIRE(yearIndex > 0);
  choose(year, yearIndex);
  CHECK(model->property("years").toList() == QVariantList{yearValue});
  REQUIRE(waitFor([&] { return !model->property("loading").toBool(); }, Timeout));
  CHECK(model->property("count").toInt() >= 1);
  choose(year, 0);
  CHECK(model->property("years").toList().isEmpty());
  REQUIRE(waitFor([&] { return model->property("count").toInt() == movies; }, Timeout));

  // Genres: any number at once, one matching nothing empties the grid.
  QObject* genres = screens.find("libraryGenre");
  REQUIRE(genres != nullptr);
  QMetaObject::invokeMethod(genres, "genreToggled",
                            Q_ARG(QString, QStringLiteral("No such genre")));
  QMetaObject::invokeMethod(genres, "genreToggled", Q_ARG(QString, QStringLiteral("Nor this")));
  CHECK(model->property("genres").toStringList() ==
        QStringList{QStringLiteral("No such genre"), QStringLiteral("Nor this")});
  REQUIRE(waitFor([&] { return model->property("count").toInt() == 0; }, Timeout));
  QMetaObject::invokeMethod(genres, "genreToggled", Q_ARG(QString, QStringLiteral("Nor this")));
  CHECK(model->property("genres").toStringList() == QStringList{QStringLiteral("No such genre")});
  QMetaObject::invokeMethod(genres, "genresCleared");
  CHECK(model->property("genres").toStringList().isEmpty());
  REQUIRE(waitFor([&] { return model->property("count").toInt() == movies; }, Timeout));
  screens.screenshot(QStringLiteral("library-filters"));

  // Collections instead of movies; a card leads to the collection page.
  QObject* show = screens.find("libraryShow");
  REQUIRE(show != nullptr);
  CHECK(show->property("visible").toBool());
  choose(show, 1);
  REQUIRE(
      waitFor([&] { return cardFor(screens, "libraryGrid", collectionId) != nullptr; }, Timeout));
  screens.screenshot(QStringLiteral("library-collections"));

  // The choice is kept: the library opens as it was left.
  REQUIRE(screens.navigator->navigate(QStringLiteral("home")));
  REQUIRE(screens.waitShowing(QStringLiteral("home")));
  REQUIRE(screens.navigator->navigate(QStringLiteral("library/") + moviesId));
  REQUIRE(screens.waitShowing(QStringLiteral("library/") + moviesId));
  REQUIRE(waitFor([&] { return libraryModel(screens) != nullptr; }, Timeout));
  CHECK(libraryModel(screens)->property("showCollections").toBool());
  REQUIRE(
      waitFor([&] { return cardFor(screens, "libraryGrid", collectionId) != nullptr; }, Timeout));
  Screens::click(cardFor(screens, "libraryGrid", collectionId));
  REQUIRE(screens.waitShowing(QStringLiteral("collection/") + collectionId));

  // The collection page: name and the movie inside.
  REQUIRE(waitFor(
      [&] {
        return screens.find("collectionName")->property("text").toString() ==
               QStringLiteral("Stand collection");
      },
      Timeout));
  REQUIRE(screens.waitCount("collectionGrid", 1));
  QQuickItem* inside = nullptr;
  REQUIRE(waitFor(
      [&] {
        inside = screens.card("collectionGrid", 0);
        return inside != nullptr && !inside->property("itemId").toString().isEmpty();
      },
      Timeout));
  CHECK(inside->property("itemId").toString() == movieId);
  CHECK(screens.find("collectionError")->property("text").toString().isEmpty());
  screens.screenshot(QStringLiteral("collection"));

  // From the movie, back to the collection holding it.
  Screens::click(inside);
  REQUIRE(screens.waitShowing(QStringLiteral("item/") + movieId));
  REQUIRE(screens.waitCount("collectionsRow", 1));
  QQuickItem* holder = nullptr;
  REQUIRE(waitFor(
      [&] {
        holder = cardFor(screens, "collectionsRow", collectionId);
        return holder != nullptr;
      },
      Timeout));
  screens.screenshot(QStringLiteral("movie-in-collection"));
  Screens::click(holder);
  REQUIRE(screens.waitShowing(QStringLiteral("collection/") + collectionId));

  // Put the switch back for the next run's profile-free state.
  REQUIRE(screens.navigator->navigate(QStringLiteral("library/") + moviesId));
  REQUIRE(screens.waitShowing(QStringLiteral("library/") + moviesId));
  REQUIRE(waitFor([&] { return libraryModel(screens) != nullptr; }, Timeout));
  libraryModel(screens)->setProperty("showCollections", false);
}
