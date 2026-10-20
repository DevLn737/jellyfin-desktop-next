#include "LibraryTestSupport.h"

#include <jfd/library/LibraryModel.h>
#include <jfd/library/LibraryUpdates.h>

#include <QSignalSpy>

using namespace jfd;
using jfd::library::ItemListModel;
using jfd::library::LibraryModel;
using jfd::library::LibraryUpdates;
using namespace jfd::test;

namespace {
  // A library of `total` movies named "Movie <n>", in pages as asked.
  struct Movies {
    int total = 250;
    int delayMs = 0;
    QString prefix = QStringLiteral("Movie");

    HttpResponse operator()(const HttpRequest& r) const {
      if (r.path == QLatin1String("/Items/Filters"))
        return ok(R"({"Genres": ["Drama", "Comedy"], "Years": [2001, 2002]})");
      if (r.path != QLatin1String("/Items")) return {404, {}, {}, 0};
      const int start = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
      const int limit = r.query.queryItemValue(QStringLiteral("limit")).toInt();
      QByteArray items;
      for (int i = start; i < std::min(total, start + limit); ++i) {
        if (!items.isEmpty()) items += ',';
        items += QStringLiteral(R"({"Id": "m%1", "Name": "%2 %1", "Type": "Movie"})")
                     .arg(i)
                     .arg(prefix)
                     .toUtf8();
      }
      return ok(page(items, total, start), delayMs);
    }
  };

  QString nameAt(LibraryModel& model, int row) {
    return model.data(model.index(row), ItemListModel::NameRole).toString();
  }
} // namespace

TEST_CASE("The grid has every row from the first page and loads pages as rows are shown") {
  Movies movies;
  LibraryFixture f([&movies](const HttpRequest& r) { return movies(r); });
  LibraryModel library(f.client);
  library.setCollectionType(QStringLiteral("movies"));
  library.setLibraryId(QStringLiteral("lib1"));
  // Both properties set in a row: one listing.
  REQUIRE(waitFor([&] { return library.count() == 250; }));
  CHECK(f.count(QStringLiteral("/Items")) == 1);
  const auto& first = f.server.requests().front();
  CHECK(first.query.queryItemValue(QStringLiteral("parentId")) == QStringLiteral("lib1"));
  CHECK(first.query.queryItemValue(QStringLiteral("includeItemTypes")) == QStringLiteral("Movie"));
  CHECK(first.query.queryItemValue(QStringLiteral("limit")) == QStringLiteral("100"));
  CHECK(library.loadedPages() == 1);
  CHECK(nameAt(library, 0) == QStringLiteral("Movie 0"));

  // A row on a page not yet loaded: a placeholder, and the page is fetched.
  CHECK_FALSE(library.data(library.index(240), LibraryModel::LoadedRole).toBool());
  CHECK(library.data(library.index(240), ItemListModel::NameRole).isNull());
  REQUIRE(waitFor([&] { return library.loadedPages() == 2; }));
  CHECK(nameAt(library, 240) == QStringLiteral("Movie 240"));
  CHECK(f.server.requests().last().query.queryItemValue(QStringLiteral("startIndex")) ==
        QStringLiteral("200"));
  // Page 1 (rows 100..199) was never shown and never loaded.
  CHECK(library.item(150) == nullptr);
  CHECK(f.count(QStringLiteral("/Items")) == 2);

  REQUIRE(waitFor([&] { return library.availableGenres().size() == 2; }));
  CHECK(library.availableYears().size() == 2);
}

TEST_CASE("Sort and filters go to the server and start the grid over") {
  Movies movies;
  LibraryFixture f([&movies](const HttpRequest& r) { return movies(r); });
  LibraryModel library(f.client);
  library.setCollectionType(QStringLiteral("movies"));
  library.setLibraryId(QStringLiteral("lib1"));
  REQUIRE(waitFor([&] { return library.count() == 250; }));

  movies.total = 3;
  library.setSortBy(QStringLiteral("CommunityRating"));
  library.setDescending(true);
  library.setPlayedFilter(LibraryModel::PlayedFilter::Unplayed);
  library.setFavoritesOnly(true);
  library.setGenres({QStringLiteral("Drama")});
  library.setYears({2001});
  REQUIRE(waitFor([&] { return library.count() == 3; }));
  QUrlQuery q;
  for (const HttpRequest& r : f.server.requests())
    if (r.path == QLatin1String("/Items")) q = r.query;
  CHECK(q.queryItemValue(QStringLiteral("sortBy")) == QStringLiteral("CommunityRating,SortName"));
  CHECK(q.queryItemValue(QStringLiteral("sortOrder")) == QStringLiteral("Descending"));
  CHECK(q.queryItemValue(QStringLiteral("isPlayed")) == QStringLiteral("false"));
  CHECK(q.queryItemValue(QStringLiteral("isFavorite")) == QStringLiteral("true"));
  CHECK(q.queryItemValue(QStringLiteral("genres")) == QStringLiteral("Drama"));
  CHECK(q.queryItemValue(QStringLiteral("years")) == QStringLiteral("2001"));
  CHECK(q.queryItemValue(QStringLiteral("startIndex")) == QStringLiteral("0"));
}

TEST_CASE("Pages of a previous listing are dropped") {
  Movies movies;
  movies.delayMs = 300;
  LibraryFixture f([&movies](const HttpRequest& r) { return movies(r); });
  LibraryModel library(f.client);
  library.setLibraryId(QStringLiteral("lib1"));
  REQUIRE(waitFor([&] { return f.count(QStringLiteral("/Items")) == 1; }));
  movies.delayMs = 0;
  movies.prefix = QStringLiteral("Sorted");
  library.setSortBy(QStringLiteral("DateCreated"));
  REQUIRE(waitFor([&] { return library.count() == 250; }));
  waitFor([] { return false; }, 400);
  CHECK(nameAt(library, 0) == QStringLiteral("Sorted 0"));
  CHECK_FALSE(library.loading());
}

TEST_CASE("A library type decides the item types; updates reach loaded rows") {
  CHECK(LibraryModel::itemTypesFor(QStringLiteral("tvshows")) ==
        QStringList{QStringLiteral("Series")});
  CHECK(LibraryModel::itemTypesFor({}).size() == 3);

  Movies movies;
  movies.total = 5;
  LibraryFixture f([&movies](const HttpRequest& r) { return movies(r); });
  LibraryModel library(f.client);
  LibraryUpdates updates;
  library.follow(updates);
  library.setLibraryId(QStringLiteral("lib1"));
  REQUIRE(waitFor([&] { return library.count() == 5; }));
  QSignalSpy changed(&library, &QAbstractItemModel::dataChanged);
  api::UserItemData data;
  data.itemId = QStringLiteral("m3");
  data.isFavorite = true;
  updates.publish(data);
  REQUIRE(changed.count() == 1);
  CHECK(library.data(library.index(3), ItemListModel::FavoriteRole).toBool());

  // An added item shifts rows: the grid starts over.
  updates.handleMessage(QStringLiteral("LibraryChanged"),
                        QJsonDocument::fromJson(R"({"ItemsAdded": ["m9"]})").object());
  movies.total = 6;
  REQUIRE(waitFor([&] { return library.count() == 6; }));
}
