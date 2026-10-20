#include "../support/DoctestQt.h"

#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ItemBrowser.h>

#include <QSignalSpy>

using namespace jfd;
using jfd::library::ItemBrowser;
using jfd::library::ItemListModel;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;

namespace {
  HttpResponse items(const char* body, int delayMs = 0) {
    return HttpResponse{200, QByteArray(body), "application/json", delayMs};
  }

  HttpResponse route(const HttpRequest& r) {
    if (r.path.endsWith(QLatin1String("/Items")) &&
        r.query.queryItemValue(QStringLiteral("searchTerm")) == QLatin1String("slow"))
      return items(R"({"Items": [{"Id": "old", "Name": "Slow result", "Type": "Movie"}],
                      "TotalRecordCount": 1})",
                   300);
    if (r.path.endsWith(QLatin1String("/Items")))
      return items(R"({"Items": [
        {"Id": "s1", "Name": "Frieren", "Type": "Series", "IsFolder": true, "ProductionYear": 2023,
         "ImageTags": {"Primary": "t1"}},
        {"Id": "m1", "Name": "Movie", "Type": "Movie", "RunTimeTicks": 72000000000}],
        "TotalRecordCount": 2})");
    if (r.path.endsWith(QLatin1String("/Shows/s1/Seasons")))
      return items(R"({"Items": [{"Id": "se1", "Name": "Season 1", "Type": "Season",
        "SeriesId": "s1", "SeriesName": "Frieren", "IndexNumber": 1}], "TotalRecordCount": 1})");
    if (r.path.endsWith(QLatin1String("/Shows/s1/Episodes")))
      return items(R"({"Items": [{"Id": "e1", "Name": "The Journey's End", "Type": "Episode",
        "SeriesId": "s1", "SeasonId": "se1", "IndexNumber": 1, "ParentIndexNumber": 1,
        "UserData": {"Played": true}}], "TotalRecordCount": 1})");
    return {404, {}, {}, 0};
  }

  QVariant cell(ItemListModel* model, int row, int role) {
    return model->data(model->index(row), role);
  }
} // namespace

TEST_CASE("Search fills the model and stepping into a series reaches an episode") {
  FakeServer server(route);
  api::JellyfinClient client(api::ClientIdentity{});
  client.setServerUrl(server.url(QStringLiteral("/jf")));
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
  ItemBrowser browser(client);
  QSignalSpy play(&browser, &ItemBrowser::playRequested);
  ItemListModel* model = browser.model();

  browser.search(QStringLiteral("frieren"));
  CHECK(browser.loading());
  REQUIRE(waitFor([&] { return model->count() == 2; }));
  CHECK_FALSE(browser.loading());
  CHECK(cell(model, 0, ItemListModel::NameRole).toString() == QStringLiteral("Frieren"));
  CHECK(cell(model, 0, ItemListModel::SubtitleRole).toString() == QStringLiteral("2023"));
  CHECK(cell(model, 0, ItemListModel::ImageUrlRole).toUrl().path() ==
        QStringLiteral("/jf/Items/s1/Images/Primary"));
  const auto& search = server.requests().front();
  CHECK(search.query.queryItemValue(QStringLiteral("userId")) == QStringLiteral("user1"));
  CHECK(search.query.queryItemValue(QStringLiteral("includeItemTypes")) ==
        QStringLiteral("Movie,Series,Episode"));

  browser.activate(0);
  REQUIRE(waitFor(
      [&] { return cell(model, 0, ItemListModel::IdRole).toString() == QLatin1String("se1"); }));
  CHECK(browser.title() == QStringLiteral("Frieren"));

  browser.activate(0);
  REQUIRE(waitFor(
      [&] { return cell(model, 0, ItemListModel::IdRole).toString() == QLatin1String("e1"); }));
  CHECK(cell(model, 0, ItemListModel::SubtitleRole).toString() == QStringLiteral("S1:E1"));
  CHECK(cell(model, 0, ItemListModel::PlayedRole).toBool());
  CHECK(server.requests().last().query.queryItemValue(QStringLiteral("seasonId")) ==
        QStringLiteral("se1"));

  browser.activate(0);
  REQUIRE(play.count() == 1);
  CHECK(play.at(0).at(0).toString() == QStringLiteral("e1"));
}

TEST_CASE("A slow answer for an earlier search never replaces a newer list") {
  FakeServer server(route);
  api::JellyfinClient client(api::ClientIdentity{});
  client.setServerUrl(server.url());
  ItemBrowser browser(client);
  browser.search(QStringLiteral("slow"));
  browser.search(QStringLiteral("frieren"));
  REQUIRE(waitFor([&] { return browser.model()->count() == 2; }));
  // Let the slow answer arrive.
  waitFor([] { return false; }, 600);
  CHECK(browser.model()->count() == 2);
  CHECK(cell(browser.model(), 0, ItemListModel::IdRole).toString() == QStringLiteral("s1"));
  CHECK_FALSE(browser.loading());
}

TEST_CASE("Errors are shown and an empty search clears the list") {
  FakeServer server([](const HttpRequest&) { return HttpResponse{500, {}, {}, 0}; });
  api::JellyfinClient client(api::ClientIdentity{});
  client.setServerUrl(server.url());
  ItemBrowser browser(client);
  browser.search(QStringLiteral("x"));
  REQUIRE(waitFor([&] { return !browser.loading(); }));
  CHECK_FALSE(browser.errorText().isEmpty());
  browser.search(QString());
  CHECK(browser.errorText().isEmpty());
  CHECK(browser.model()->count() == 0);
}
