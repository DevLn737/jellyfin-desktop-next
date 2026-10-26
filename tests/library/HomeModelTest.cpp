#include "LibraryTestSupport.h"

#include <jfd/library/HomeModel.h>
#include <jfd/library/LibraryUpdates.h>

#include <QSignalSpy>

using namespace jfd;
using jfd::library::HomeModel;
using jfd::library::ItemListModel;
using jfd::library::LibraryUpdates;
using namespace jfd::test;

namespace {
  struct Server {
    bool resumeEmpty = false;
    int latestDelayMs = 0;
    QByteArray latestName = "Frieren";

    HttpResponse operator()(const HttpRequest& r) const {
      if (r.path == QLatin1String("/UserViews"))
        return ok(page(R"({"Id": "shows", "Name": "Shows", "Type": "CollectionFolder",
                            "CollectionType": "tvshows"},
                           {"Id": "music", "Name": "Music", "Type": "CollectionFolder",
                            "CollectionType": "music"},
                           {"Id": "movies", "Name": "Movies", "Type": "CollectionFolder",
                            "CollectionType": "movies"})"));
      if (r.path == QLatin1String("/UserItems/Resume"))
        return ok(resumeEmpty ? page("") : page(R"({"Id": "e5", "Name": "Ep 5", "Type": "Episode",
                                         "SeriesId": "s1", "RunTimeTicks": 1000,
                                         "ImageTags": {"Primary": "p5"},
                                         "UserData": {"PlaybackPositionTicks": 250}})"));
      if (r.path == QLatin1String("/Shows/NextUp")) return ok(page(""));
      if (r.path == QLatin1String("/Items/Latest")) {
        const QString parent = r.query.queryItemValue(QStringLiteral("parentId"));
        if (parent == QLatin1String("shows"))
          return ok(R"([{"Id": "s1", "Name": ")" + latestName +
                        R"(", "Type": "Series", "ImageTags": {"Primary": "sp"}}])",
                    latestDelayMs);
        return ok(R"([{"Id": "m1", "Name": "Movie", "Type": "Movie"}])", latestDelayMs);
      }
      return {404, {}, {}, 0};
    }
  };
} // namespace

TEST_CASE("Home rows come in Jellyfin Web's order and empty rows are left out") {
  Server routes;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeModel home(f.client);
  home.reload();
  CHECK(home.loading());
  REQUIRE(waitFor([&] { return !home.loading(); }));
  CHECK(home.errorText().isEmpty());

  // Music has no "latest" row; "Next up" is empty and hidden.
  REQUIRE(home.count() == 3);
  CHECK(home.rowKind(0) == QStringLiteral("resume"));
  CHECK(home.data(home.index(0), HomeModel::TitleRole).toString() ==
        QStringLiteral("Continue watching"));
  CHECK(home.data(home.index(0), HomeModel::CardShapeRole).toString() == QStringLiteral("thumb"));
  CHECK(home.data(home.index(1), HomeModel::TitleRole).toString() ==
        QStringLiteral("Latest in Shows"));
  CHECK(home.data(home.index(2), HomeModel::LibraryIdRole).toString() == QStringLiteral("movies"));
  CHECK(f.count(QStringLiteral("/Items/Latest")) == 2);
  CHECK(home.libraries()->count() == 3);

  ItemListModel* resume = home.rowItems(0);
  REQUIRE(resume->count() == 1);
  CHECK(resume->data(resume->index(0), ItemListModel::ProgressRole).toDouble() ==
        doctest::Approx(0.25));
  // A wide card for an episode uses its own still, sized for the card.
  const QUrl image = resume->data(resume->index(0), ItemListModel::ImageUrlRole).toUrl();
  CHECK(image.path() == QStringLiteral("/Items/e5/Images/Primary"));
  CHECK(QUrlQuery(image).queryItemValue(QStringLiteral("fillWidth")) == QStringLiteral("200"));
  CHECK(QUrlQuery(image).queryItemValue(QStringLiteral("fillHeight")) == QStringLiteral("113"));
}

TEST_CASE("A reload keeps rows on screen and drops what became empty") {
  Server routes;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeModel home(f.client);
  home.reload();
  REQUIRE(waitFor([&] { return !home.loading(); }));
  REQUIRE(home.count() == 3);
  ItemListModel* latestShows = home.rowItems(1);

  routes.resumeEmpty = true;
  routes.latestName = "Frieren 2";
  QSignalSpy reset(&home, &QAbstractItemModel::modelReset);
  QSignalSpy removed(&home, &QAbstractItemModel::rowsRemoved);
  home.reload();
  // Until the answers come, nothing changes.
  CHECK(home.count() == 3);
  REQUIRE(waitFor([&] { return !home.loading(); }));
  CHECK(reset.isEmpty());
  CHECK(removed.count() == 1);
  REQUIRE(home.count() == 2);
  // The same row object, refilled.
  CHECK(home.rowItems(0) == latestShows);
  CHECK(latestShows->data(latestShows->index(0), ItemListModel::NameRole).toString() ==
        QStringLiteral("Frieren 2"));
}

TEST_CASE("Answers of an older reload are dropped") {
  Server routes;
  routes.latestDelayMs = 300;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeModel home(f.client);
  home.reload();
  REQUIRE(waitFor([&] { return f.count(QStringLiteral("/Items/Latest")) == 2; }));
  routes.latestDelayMs = 0;
  routes.latestName = "Newer";
  home.reload();
  REQUIRE(waitFor([&] { return !home.loading(); }));
  // Let the slow answers of the first reload arrive.
  waitFor([] { return false; }, 400);
  ItemListModel* shows = home.rowItems(1);
  CHECK(shows->data(shows->index(0), ItemListModel::NameRole).toString() ==
        QStringLiteral("Newer"));
}

TEST_CASE("Watched state from the server updates the cards and reloads the rows") {
  Server routes;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeModel::Options options;
  options.updateDelayMs = 50;
  HomeModel home(f.client, options);
  LibraryUpdates updates;
  home.follow(updates);
  home.reload();
  REQUIRE(waitFor([&] { return !home.loading(); }));
  const int resumeCalls = f.count(QStringLiteral("/UserItems/Resume"));

  updates.handleMessage(QStringLiteral("UserDataChanged"),
                        QJsonDocument::fromJson(R"({"UserId": "user1", "UserDataList": [
                          {"ItemId": "s1", "IsFavorite": true, "Played": true}]})")
                            .object());
  ItemListModel* shows = home.rowItems(1);
  CHECK(shows->data(shows->index(0), ItemListModel::FavoriteRole).toBool());
  CHECK(shows->data(shows->index(0), ItemListModel::PlayedRole).toBool());
  // Several updates in a row cost one reload.
  updates.handleMessage(QStringLiteral("UserDataChanged"),
                        QJsonDocument::fromJson(R"({"UserDataList": [
                          {"ItemId": "e5", "PlaybackPositionTicks": 400}]})")
                            .object());
  REQUIRE(waitFor([&] { return f.count(QStringLiteral("/UserItems/Resume")) == resumeCalls + 1; }));
  waitFor([] { return false; }, 150);
  CHECK(f.count(QStringLiteral("/UserItems/Resume")) == resumeCalls + 1);
}

TEST_CASE("The user's arrangement orders the rows, hides some and tolerates unknown ones") {
  Server routes;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeModel home(f.client);
  home.reload();
  REQUIRE(waitFor([&] { return !home.loading(); }));
  REQUIRE(home.count() == 3);
  const auto id = [&](int row) { return home.data(home.index(row), HomeModel::SectionIdRole).toString(); };
  CHECK(id(0) == QStringLiteral("resume"));

  jfd::library::HomeArrangement arrangement;
  arrangement.order = {QStringLiteral("gone"), QStringLiteral("latest/movies"), QStringLiteral("resume")};
  arrangement.hidden = {QStringLiteral("resume")};
  home.setArrangement(arrangement);
  REQUIRE(home.count() == 2);
  CHECK(id(0) == QStringLiteral("latest/movies"));
  CHECK(id(1) == QStringLiteral("latest/shows"));

  home.setSectionVisible(QStringLiteral("resume"), true);
  CHECK(home.count() == 3);
  QSignalSpy saved(&home, &HomeModel::arrangementChanged);
  home.moveSection(0, 2);
  CHECK(saved.count() == 1);

  home.resetArrangement();
  CHECK(id(0) == QStringLiteral("resume"));
  CHECK(home.arrangement() == jfd::library::HomeArrangement{});
}
