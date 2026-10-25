#include "LibraryTestSupport.h"

#include <jfd/library/CollectionIndex.h>
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
      // The library's own name.
      if (r.path == QLatin1String("/Items/lib1")) return ok(R"({"Id": "lib1", "Name": "Films"})");
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
  HttpRequest first;
  for (const HttpRequest& r : f.server.requests()) {
    if (r.path == QLatin1String("/Items")) {
      first = r;
      break;
    }
  }
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

namespace {
  // Last query of a path.
  QUrlQuery lastQuery(const LibraryFixture& f, const QString& path) {
    QUrlQuery q;
    for (const HttpRequest& r : f.server.requests())
      if (r.path == path) q = r.query;
    return q;
  }
} // namespace

TEST_CASE("The library's name comes from the server") {
  Movies movies;
  LibraryFixture f([&movies](const HttpRequest& r) { return movies(r); });
  LibraryModel library(f.client);
  library.setLibraryId(QStringLiteral("lib1"));
  CHECK(library.name().isEmpty());
  REQUIRE(waitFor([&] { return library.name() == QStringLiteral("Films"); }));
}

TEST_CASE("Sort and filters can be kept and applied again") {
  Movies movies;
  movies.total = 3;
  LibraryFixture f([&movies](const HttpRequest& r) { return movies(r); });
  LibraryModel one(f.client);
  one.setCollectionType(QStringLiteral("movies"));
  one.setLibraryId(QStringLiteral("lib1"));
  one.setSortBy(QStringLiteral("PremiereDate"));
  one.setDescending(true);
  one.setPlayedFilter(LibraryModel::PlayedFilter::Played);
  one.setFavoritesOnly(true);
  one.setGenres({QStringLiteral("Drama"), QStringLiteral("Comedy")});
  one.setYears({2001});
  one.setShowCollections(true);
  const QJsonObject kept = one.filters();

  // Through JSON, as the settings keep it.
  const QJsonObject stored = QJsonDocument::fromJson(QJsonDocument(kept).toJson()).object();
  LibraryModel two(f.client);
  two.setCollectionType(QStringLiteral("movies"));
  two.applyFilters(stored);
  CHECK(two.sortBy() == QStringLiteral("PremiereDate"));
  CHECK(two.descending());
  CHECK(two.playedFilter() == LibraryModel::PlayedFilter::Played);
  CHECK(two.favoritesOnly());
  CHECK(two.genres() == QStringList{QStringLiteral("Drama"), QStringLiteral("Comedy")});
  CHECK(two.years() == QVariantList{2001});
  CHECK(two.showCollections());
  CHECK(two.filters() == kept);

  // Missing or foreign keys change nothing.
  two.applyFilters({});
  two.applyFilters(QJsonObject{{QStringLiteral("sortBy"), 5}, {QStringLiteral("genres"), 7}});
  CHECK(two.filters() == kept);
}

TEST_CASE("A movie library can list its collections; a collection lists its own items") {
  Movies movies;
  movies.total = 2;
  LibraryFixture f([&movies](const HttpRequest& r) { return movies(r); });
  LibraryModel library(f.client);
  library.setCollectionType(QStringLiteral("movies"));
  library.setLibraryId(QStringLiteral("lib1"));
  CHECK(library.canShowCollections());
  library.setShowCollections(true);
  REQUIRE(waitFor([&] { return library.count() == 2; }));
  CHECK(lastQuery(f, QStringLiteral("/Items")).queryItemValue(QStringLiteral("includeItemTypes")) ==
        QStringLiteral("BoxSet"));
  CHECK(lastQuery(f, QStringLiteral("/Items/Filters"))
            .queryItemValue(QStringLiteral("includeItemTypes")) == QStringLiteral("BoxSet"));

  // A show library has no such switch, and ignores it.
  LibraryModel shows(f.client);
  shows.setCollectionType(QStringLiteral("tvshows"));
  shows.setShowCollections(true);
  CHECK_FALSE(shows.canShowCollections());
  shows.setLibraryId(QStringLiteral("lib1"));
  REQUIRE(waitFor([&] { return shows.count() == 2; }));
  CHECK(lastQuery(f, QStringLiteral("/Items")).queryItemValue(QStringLiteral("includeItemTypes")) ==
        QStringLiteral("Series"));

  // A collection: whatever it holds, one level down.
  LibraryModel collection(f.client);
  collection.setCollectionType(LibraryModel::CollectionItems);
  collection.setLibraryId(QStringLiteral("lib1"));
  REQUIRE(waitFor([&] { return collection.count() == 2; }));
  const QUrlQuery q = lastQuery(f, QStringLiteral("/Items"));
  CHECK(q.queryItemValue(QStringLiteral("parentId")) == QStringLiteral("lib1"));
  CHECK(q.queryItemValue(QStringLiteral("includeItemTypes")).isEmpty());
  CHECK(q.queryItemValue(QStringLiteral("recursive")) == QStringLiteral("false"));
}

TEST_CASE("The collection index finds the collections holding an item") {
  int collectionListings = 0;
  QStringList b2Items{QStringLiteral("m2")};
  LibraryFixture f([&](const HttpRequest& r) {
    if (r.path != QLatin1String("/Items")) return HttpResponse{404, {}, {}, 0};
    const QString parent = r.query.queryItemValue(QStringLiteral("parentId"));
    if (parent.isEmpty()) {
      ++collectionListings;
      return ok(page(R"({"Id": "b1", "Name": "Alpha", "Type": "BoxSet"},)"
                     R"({"Id": "b2", "Name": "Beta", "Type": "BoxSet"})"));
    }
    if (parent == QLatin1String("b1")) return ok(page(R"({"Id": "m1"},{"Id": "m2"})"));
    QByteArray items;
    for (const QString& id : b2Items)
      items += (items.isEmpty() ? "" : ",") + QByteArray(R"({"Id": ")") + id.toUtf8() + "\"}";
    return ok(page(items));
  });
  library::CollectionIndex index(f.client);

  auto ask = [&](const QString& id) {
    QStringList names;
    bool answered = false;
    index.collectionsOf(id, &f.client, [&](const QList<api::Item>& collections) {
      for (const api::Item& item : collections)
        names.append(item.name);
      answered = true;
    });
    REQUIRE(waitFor([&] { return answered; }));
    return names;
  };

  CHECK(ask(QStringLiteral("m2")) == QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta")});
  CHECK(ask(QStringLiteral("m1")) == QStringList{QStringLiteral("Alpha")});
  CHECK(ask(QStringLiteral("m9")).isEmpty());
  CHECK(collectionListings == 1);

  // A library change builds it again on the next question.
  library::LibraryUpdates updates;
  index.follow(updates);
  b2Items = {QStringLiteral("m9")};
  updates.handleMessage(QStringLiteral("LibraryChanged"),
                        QJsonDocument::fromJson(R"({"ItemsUpdated": ["b2"]})").object());
  CHECK_FALSE(index.ready());
  CHECK(ask(QStringLiteral("m9")) == QStringList{QStringLiteral("Beta")});
  CHECK(collectionListings == 2);
}
