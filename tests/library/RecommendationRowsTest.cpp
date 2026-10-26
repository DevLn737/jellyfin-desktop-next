#include "LibraryTestSupport.h"

#include <jfd/library/HomeSection.h>
#include <jfd/library/RecommendationRows.h>

#include <QTest>

using namespace jfd;
using jfd::library::HomeSections;
using jfd::library::RecommendationRows;
using namespace jfd::test;

namespace {
  struct Server {
    bool movies404 = false;
    bool noHistory = false;
    int picksDelayMs = 0;

    HttpResponse operator()(const HttpRequest& r) const {
      if (r.path == QLatin1String("/Movies/Recommendations")) {
        if (movies404) return {404, {}, {}, 0};
        return ok(R"([{"RecommendationType": "SimilarToRecentlyPlayed", "BaselineItemName": "Arrival",
                      "CategoryId": "c1", "Items": [{"Id": "m1", "Name": "Sicario", "Type": "Movie"}]},
                     {"RecommendationType": "HasDirectorFromRecentlyPlayed", "BaselineItemName": "Villeneuve",
                      "CategoryId": "c2", "Items": []}])");
      }
      if (r.path == QLatin1String("/Items") && r.query.queryItemValue(QStringLiteral("isPlayed")) == QLatin1String("true")) {
        if (noHistory) return ok(page(""));
        return ok(page(R"({"Id": "e1", "Name": "Ep", "Type": "Episode", "SeriesId": "s1", "SeriesName": "Frieren"},
                         {"Id": "e2", "Name": "Ep", "Type": "Episode", "SeriesId": "s1", "SeriesName": "Frieren"})"));
      }
      if (r.path == QLatin1String("/Items/s1/Similar"))
        return ok(page(R"({"Id": "s2", "Name": "Mushishi", "Type": "Series"})"));
      if (r.path == QLatin1String("/Items/Suggestions"))
        return ok(page(R"({"Id": "p1", "Name": "Pick", "Type": "Movie"})"), picksDelayMs);
      return {404, {}, {}, 0};
    }
  };

  RecommendationRows::Options fast() {
    RecommendationRows::Options options;
    options.timeoutMs = 400;
    return options;
  }
} // namespace

TEST_CASE("Recommendation rows are registered as sections and empty ones are left out") {
  Server routes;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeSections sections;
  RecommendationRows rows(f.client, sections, fast());
  rows.refresh();
  REQUIRE(waitFor([&] { return sections.contains(QStringLiteral("recommend/picks")); }));
  REQUIRE(waitFor([&] { return rows.sectionIds().size() == 3; }));

  CHECK(sections.contains(QStringLiteral("recommend/movie/c1")));
  CHECK_FALSE(sections.contains(QStringLiteral("recommend/movie/c2")));
  CHECK(sections.contains(QStringLiteral("recommend/series/s1")));
  CHECK(sections.find(QStringLiteral("recommend/movie/c1"))->title == QStringLiteral("Because you watched Arrival"));
  CHECK(sections.find(QStringLiteral("recommend/series/s1"))->title == QStringLiteral("Because you watched Frieren"));
  CHECK(sections.find(QStringLiteral("recommend/picks"))->title == QStringLiteral("Picks"));
  CHECK(sections.find(QStringLiteral("recommend/picks"))->model->rowCount() == 1);
}

TEST_CASE("A server without recommendations leaves the other rows alone") {
  Server routes;
  routes.movies404 = true;
  routes.noHistory = true;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeSections sections;
  RecommendationRows rows(f.client, sections, fast());
  rows.refresh();
  REQUIRE(waitFor([&] { return sections.contains(QStringLiteral("recommend/picks")); }));
  CHECK(rows.sectionIds() == QStringList{QStringLiteral("recommend/picks")});
}

TEST_CASE("A slow answer is left out after the timeout") {
  Server routes;
  routes.picksDelayMs = 2000;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeSections sections;
  RecommendationRows rows(f.client, sections, fast());
  rows.refresh();
  REQUIRE(waitFor([&] { return sections.contains(QStringLiteral("recommend/movie/c1")); }));
  QTest::qWait(700);
  CHECK_FALSE(sections.contains(QStringLiteral("recommend/picks")));
  CHECK(sections.contains(QStringLiteral("recommend/series/s1")));
}

TEST_CASE("The rows are fetched again no sooner than the refresh interval") {
  Server routes;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeSections sections;
  RecommendationRows rows(f.client, sections, fast());
  rows.refresh();
  REQUIRE(waitFor([&] { return rows.sectionIds().size() == 3; }));
  QTest::qWait(100);
  const int first = f.count(QStringLiteral("/Items/Suggestions"));
  rows.refresh();
  QTest::qWait(200);
  CHECK(f.count(QStringLiteral("/Items/Suggestions")) == first);
  rows.refresh(true);
  REQUIRE(waitFor([&] { return f.count(QStringLiteral("/Items/Suggestions")) == first + 1; }));
}

TEST_CASE("Clearing forgets the rows") {
  Server routes;
  LibraryFixture f([&routes](const HttpRequest& r) { return routes(r); });
  HomeSections sections;
  RecommendationRows rows(f.client, sections, fast());
  rows.refresh();
  REQUIRE(waitFor([&] { return rows.sectionIds().size() == 3; }));
  rows.clear();
  CHECK(rows.sectionIds().isEmpty());
  CHECK_FALSE(sections.contains(QStringLiteral("recommend/picks")));
}
