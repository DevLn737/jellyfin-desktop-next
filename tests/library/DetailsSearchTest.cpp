#include "LibraryTestSupport.h"

#include <jfd/library/ItemDetails.h>
#include <jfd/library/LibraryUpdates.h>
#include <jfd/library/SearchModel.h>

#include <QSignalSpy>

using namespace jfd;
using jfd::library::ItemDetails;
using jfd::library::ItemListModel;
using jfd::library::LibraryUpdates;
using jfd::library::SearchModel;
using namespace jfd::test;

namespace {
  struct Show {
    bool nextUp = true;
    bool favorite = false;

    HttpResponse operator()(const HttpRequest& r) {
      const QString p = r.path;
      if (p == QLatin1String("/Items/s1"))
        return ok(R"({"Id": "s1", "Name": "Frieren", "Type": "Series", "IsFolder": true,
                      "Genres": ["Fantasy"], "ProductionYear": 2023, "CommunityRating": 9.1,
                      "ImageTags": {"Primary": "p", "Logo": "l"}, "BackdropImageTags": ["b"]})");
      if (p == QLatin1String("/Items/se2"))
        return ok(R"({"Id": "se2", "Name": "Season 2", "Type": "Season", "IsFolder": true,
                      "SeriesId": "s1", "SeriesName": "Frieren"})");
      if (p == QLatin1String("/Items/m1"))
        return ok(R"({"Id": "m1", "Name": "Movie", "Type": "Movie", "RunTimeTicks": 100,
                      "UserData": {"PlaybackPositionTicks": 40}})");
      if (p == QLatin1String("/Shows/s1/Seasons"))
        return ok(page(R"({"Id": "se1", "Name": "Season 1", "Type": "Season", "SeriesId": "s1"},
                          {"Id": "se2", "Name": "Season 2", "Type": "Season", "SeriesId": "s1"})"));
      if (p == QLatin1String("/Shows/s1/Episodes")) {
        if (r.query.queryItemValue(QStringLiteral("seasonId")) == QLatin1String("se2"))
          return ok(page(R"({"Id": "e21", "Name": "Ep 1", "Type": "Episode", "SeasonId": "se2",
                              "SeriesId": "s1", "UserData": {"Played": true}},
                             {"Id": "e22", "Name": "Ep 2", "Type": "Episode", "SeasonId": "se2",
                              "SeriesId": "s1", "UserData": {"PlaybackPositionTicks": 70}})"));
        return ok(page(R"({"Id": "e11", "Name": "Ep 1", "Type": "Episode", "SeasonId": "se1",
                            "SeriesId": "s1"})"));
      }
      if (p == QLatin1String("/Shows/NextUp"))
        return ok(nextUp ? page(R"({"Id": "e22", "Name": "Ep 2", "Type": "Episode",
                                    "SeasonId": "se2", "SeriesId": "s1",
                                    "UserData": {"PlaybackPositionTicks": 70}})")
                         : page(""));
      if (p.endsWith(QLatin1String("/Similar"))) return ok(page(R"({"Id": "x", "Name": "X"})"));
      if (p.startsWith(QLatin1String("/UserFavoriteItems/"))) {
        favorite = r.method == "POST";
        return ok(QStringLiteral(R"({"ItemId": "%1", "IsFavorite": %2})")
                      .arg(p.section(QLatin1Char('/'), 2),
                           favorite ? QStringLiteral("true") : QStringLiteral("false"))
                      .toUtf8());
      }
      if (p.startsWith(QLatin1String("/UserPlayedItems/"))) return ok("{}");
      return {404, {}, {}, 0};
    }
  };
} // namespace

TEST_CASE("A series page plays its next-up episode and shows that episode's season") {
  Show show;
  LibraryFixture f([&show](const HttpRequest& r) { return show(r); });
  ItemDetails page(f.client);
  QSignalSpy play(&page, &ItemDetails::playRequested);
  page.setItemId(QStringLiteral("s1"));
  REQUIRE(waitFor([&] { return page.loaded() && !page.loading(); }));
  CHECK(page.name() == QStringLiteral("Frieren"));
  CHECK(page.genres() == QStringList{QStringLiteral("Fantasy")});
  CHECK(page.year().toInt() == 2023);
  CHECK(page.backdropUrl().path() == QStringLiteral("/Items/s1/Images/Backdrop/0"));
  CHECK(page.logoUrl().path() == QStringLiteral("/Items/s1/Images/Logo"));
  CHECK(page.seasons()->count() == 2);
  CHECK(page.seasonId() == QStringLiteral("se2"));
  REQUIRE(page.episodes()->count() == 2);
  CHECK(page.playTargetId() == QStringLiteral("e22"));
  CHECK(page.resumePositionTicks() == 70);
  CHECK(page.similar()->count() == 1);
  CHECK(f.server.requests()
            .front()
            .query.queryItemValue(QStringLiteral("fields"))
            .contains(QStringLiteral("Trickplay")));

  page.resume();
  page.play();
  page.watchTogether();
  REQUIRE(play.count() == 3);
  CHECK(play[0][0].toString() == QStringLiteral("e22"));
  CHECK(play[0][1].toLongLong() == 70);
  CHECK(play[1][1].toLongLong() == 0);
  CHECK(play[2][2].toBool());

  // Choosing another season lists its episodes.
  page.selectSeason(QStringLiteral("se1"));
  REQUIRE(waitFor([&] {
    return page.episodes()->count() == 1 &&
           page.episodes()->entry(0)->item.id == QLatin1String("e11");
  }));
  page.playEpisode(0);
  CHECK(play.last()[0].toString() == QStringLiteral("e11"));
}

TEST_CASE("Without next-up a series plays its first episode; a season its first unwatched") {
  Show show;
  show.nextUp = false;
  LibraryFixture f([&show](const HttpRequest& r) { return show(r); });
  ItemDetails series(f.client);
  series.setItemId(QStringLiteral("s1"));
  REQUIRE(waitFor([&] { return series.loaded() && !series.loading(); }));
  CHECK(series.seasonId() == QStringLiteral("se1"));
  CHECK(series.playTargetId() == QStringLiteral("e11"));

  ItemDetails season(f.client);
  season.setItemId(QStringLiteral("se2"));
  REQUIRE(waitFor([&] { return season.loaded() && !season.loading(); }));
  CHECK(season.seasonId() == QStringLiteral("se2"));
  CHECK(season.playTargetId() == QStringLiteral("e22"));
  CHECK(season.seriesName() == QStringLiteral("Frieren"));
}

TEST_CASE("Favorite and watched toggles reach every model through the updates") {
  Show show;
  LibraryFixture f([&show](const HttpRequest& r) { return show(r); });
  LibraryUpdates updates;
  ItemDetails movie(f.client);
  movie.setUpdates(&updates);
  QSignalSpy published(&updates, &LibraryUpdates::userDataChanged);
  movie.setItemId(QStringLiteral("m1"));
  REQUIRE(waitFor([&] { return movie.loaded(); }));
  CHECK(movie.playTargetId() == QStringLiteral("m1"));
  CHECK(movie.progress() == doctest::Approx(0.4));

  movie.toggleFavorite();
  REQUIRE(waitFor([&] { return movie.favorite(); }));
  CHECK(show.favorite);
  CHECK(published.count() == 1);

  movie.togglePlayed();
  REQUIRE(waitFor([&] { return movie.played(); }));
  CHECK(movie.playbackPositionTicks() == 0);
  CHECK(f.server.requests().last().method == QByteArray("POST"));
  CHECK(f.server.requests().last().path == QStringLiteral("/UserPlayedItems/m1"));
}

TEST_CASE("Search waits for typing to pause and shows only the latest answer") {
  LibraryFixture f([](const HttpRequest& r) {
    if (r.path == QLatin1String("/Persons")) return ok(page(""));
    const QString term = r.query.queryItemValue(QStringLiteral("searchTerm"));
    if (term == QLatin1String("slow"))
      return ok(page(R"({"Id": "old", "Name": "Old", "Type": "Movie"})"), 400);
    return ok(
        page(QStringLiteral(R"({"Id": "n", "Name": "%1", "Type": "Movie"})").arg(term).toUtf8()));
  });
  SearchModel::Options options;
  options.delayMs = 100;
  SearchModel search(f.client, options);
  search.setText(QStringLiteral("f"));
  search.setText(QStringLiteral("fr"));
  search.setText(QStringLiteral("fri"));
  REQUIRE(waitFor([&] { return search.results()->count() == 1; }));
  // One search for titles and music, one for people.
  CHECK(f.count(QStringLiteral("/Items")) == 1);
  CHECK(f.count(QStringLiteral("/Persons")) == 1);
  CHECK(search.results()->entry(0)->item.name == QStringLiteral("fri"));

  // A slow answer for an earlier text never replaces a newer one.
  search.setText(QStringLiteral("slow"));
  search.submit();
  search.setText(QStringLiteral("fast"));
  search.submit();
  REQUIRE(waitFor([&] {
    return search.results()->count() == 1 &&
           search.results()->entry(0)->item.name == QLatin1String("fast");
  }));
  waitFor([] { return false; }, 500);
  CHECK(search.results()->entry(0)->item.name == QStringLiteral("fast"));
  CHECK_FALSE(search.loading());

  // Clearing is immediate.
  search.setText(QString());
  CHECK(search.results()->count() == 0);
}

TEST_CASE("Search finds tracks and people; people come last") {
  bool peopleFail = false;
  LibraryFixture f([&peopleFail](const HttpRequest& r) {
    if (r.path == QLatin1String("/Persons")) {
      if (peopleFail) return HttpResponse{500, "{}"};
      // The people answer comes first; the list still keeps them last.
      return ok(page(R"({"Id": "p1", "Name": "Some Actor", "Type": "Person",
                         "ImageTags": {"Primary": "pp"}})"));
    }
    return ok(page(R"({"Id": "t1", "Name": "Track", "Type": "Audio", "MediaType": "Audio",
                       "AlbumId": "al1", "Album": "Album", "AlbumPrimaryImageTag": "at",
                       "Artists": ["Band", "Guest"]},
                      {"Id": "m1", "Name": "Movie", "Type": "Movie"})"),
              150);
  });
  SearchModel::Options options;
  options.delayMs = 10;
  options.peopleLimit = 7;
  SearchModel search(f.client, options);
  search.setText(QStringLiteral("k"));
  REQUIRE(waitFor([&] { return search.results()->count() == 3 && !search.loading(); }));
  const ItemListModel& results = *search.results();
  CHECK(results.entry(0)->item.type == QStringLiteral("Audio"));
  CHECK(results.entry(2)->item.type == QStringLiteral("Person"));
  const QModelIndex track = results.index(0);
  CHECK(results.data(track, ItemListModel::AlbumIdRole).toString() == QStringLiteral("al1"));
  CHECK(results.data(track, ItemListModel::SubtitleRole).toString() == QStringLiteral("Band, Guest"));
  // A track without a cover of its own shows its album's.
  CHECK(results.entry(0)->imageUrl.path() == QStringLiteral("/Items/al1/Images/Primary"));

  const HttpRequest* items = nullptr;
  const HttpRequest* people = nullptr;
  for (const HttpRequest& r : f.server.requests()) {
    if (r.path == QLatin1String("/Items")) items = &r;
    if (r.path == QLatin1String("/Persons")) people = &r;
  }
  REQUIRE(items != nullptr);
  REQUIRE(people != nullptr);
  CHECK(items->query.queryItemValue(QStringLiteral("includeItemTypes"))
            .split(QLatin1Char(','))
            .contains(QStringLiteral("Audio")));
  CHECK(people->query.queryItemValue(QStringLiteral("searchTerm")) == QStringLiteral("k"));
  CHECK(people->query.queryItemValue(QStringLiteral("limit")) == QStringLiteral("7"));
  CHECK(people->query.queryItemValue(QStringLiteral("userId")) == QStringLiteral("user1"));

  // Without the people search the rest is still found, with no error.
  peopleFail = true;
  search.setText(QStringLiteral("ke"));
  REQUIRE(waitFor([&] {
    return search.results()->count() == 2 && !search.loading() &&
           f.count(QStringLiteral("/Persons")) == 2;
  }));
  CHECK(search.errorText().isEmpty());
}

TEST_CASE("Socket messages become user data and library changes") {
  LibraryUpdates updates;
  QSignalSpy data(&updates, &LibraryUpdates::userDataChanged);
  QSignalSpy library(&updates, &LibraryUpdates::libraryChanged);
  updates.handleMessage(QStringLiteral("UserDataChanged"),
                        QJsonDocument::fromJson(R"({"UserDataList": [
                          {"ItemId": "a", "Played": true}, {"ItemId": "b", "IsFavorite": true},
                          {"Played": true}]})")
                            .object());
  REQUIRE(data.count() == 2);
  CHECK(data[1][0].value<api::UserItemData>().isFavorite);
  updates.handleMessage(
      QStringLiteral("LibraryChanged"),
      QJsonDocument::fromJson(R"({"ItemsAdded": [], "ItemsRemoved": []})").object());
  CHECK(library.isEmpty());
  updates.handleMessage(QStringLiteral("LibraryChanged"),
                        QJsonDocument::fromJson(R"({"ItemsUpdated": ["x"]})").object());
  REQUIRE(library.count() == 1);
  CHECK(library[0][1].toStringList() == QStringList{QStringLiteral("x")});
  updates.handleMessage(QStringLiteral("KeepAlive"), {});
  CHECK(data.count() == 2);
}
