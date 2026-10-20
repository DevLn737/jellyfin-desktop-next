#include "../support/DoctestQt.h"

#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/ServerDiscovery.h>

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QUdpSocket>

#include <optional>

using namespace jfd;
using namespace jfd::api;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;

namespace {
  template <class T> Result<T> run(std::function<void(Callback<T>)> call) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    REQUIRE(waitFor([&] { return out.has_value(); }));
    return std::move(*out);
  }

  struct Signed {
    FakeServer server;
    JellyfinClient client{ClientIdentity{}};

    explicit Signed(FakeServer::Handler handler) : server(std::move(handler)) {
      client.setServerUrl(server.url(QStringLiteral("/jf")));
      client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
    }
    const HttpRequest& last() const { return server.requests().last(); }
    QString param(const char* name) const {
      return last().query.queryItemValue(QString::fromLatin1(name));
    }
  };

  const char* EpisodeJson = R"({
    "Id": "e1", "Name": "Episode", "Type": "Episode", "SeriesId": "s1",
    "Genres": ["Drama", "Fantasy"], "CommunityRating": 8.5, "OfficialRating": "TV-14",
    "PrimaryImageAspectRatio": 1.7777, "ImageTags": {"Primary": "p1", "Thumb": "t1"},
    "BackdropImageTags": [], "SeriesPrimaryImageTag": "sp1",
    "ParentBackdropItemId": "s1", "ParentBackdropImageTags": ["b1", "b2"],
    "UserData": {"PlaybackPositionTicks": 50000000, "Played": false, "IsFavorite": true,
                 "PlayedPercentage": 12.5, "ItemId": "e1"},
    "Trickplay": {"e1": {
      "320": {"Width": 320, "Height": 180, "TileWidth": 10, "TileHeight": 10,
              "ThumbnailCount": 250, "Interval": 10000, "Bandwidth": 9000},
      "160": {"Width": 160, "Height": 90, "TileWidth": 10, "TileHeight": 10,
              "ThumbnailCount": 250, "Interval": 10000, "Bandwidth": 2000},
      "0": {"Width": 0}}}
  })";
} // namespace

TEST_CASE("Item details carry genres, images, user data and trickplay") {
  Signed s([](const HttpRequest&) { return jfd::test::json(EpisodeJson); });
  auto item =
      run<Item>([&](auto done) { s.client.itemDetails(QStringLiteral("e1"), nullptr, done); });
  REQUIRE(item.ok());
  CHECK(s.last().path == QStringLiteral("/jf/Items/e1"));
  CHECK(s.param("fields").split(QLatin1Char(',')).contains(QStringLiteral("Trickplay")));
  CHECK(item->genres == QStringList{QStringLiteral("Drama"), QStringLiteral("Fantasy")});
  CHECK(item->communityRating == doctest::Approx(8.5));
  CHECK(item->officialRating == QStringLiteral("TV-14"));
  CHECK(item->primaryImageTag == QStringLiteral("p1"));
  CHECK(item->imageTags.value(QStringLiteral("Thumb")) == QStringLiteral("t1"));
  CHECK(item->seriesPrimaryImageTag == QStringLiteral("sp1"));
  CHECK(item->parentBackdropImageTags.size() == 2);
  CHECK(item->userData.isFavorite);
  CHECK(item->userData.playedPercentage == doctest::Approx(12.5));

  // Invalid resolutions are dropped, the rest sorted by width.
  const auto resolutions = item->trickplay.value(QStringLiteral("e1"));
  REQUIRE(resolutions.size() == 2);
  CHECK(resolutions[0].width == 160);
  CHECK(resolutions[1].width == 320);
  CHECK(resolutions[1].tileCount() == 3);
}

TEST_CASE("Trickplay positions map to a tile and a rectangle in it") {
  TrickplayInfo info;
  info.width = 320;
  info.height = 180;
  info.tileWidth = 10;
  info.tileHeight = 10;
  info.thumbnailCount = 250;
  info.intervalMs = 10000;

  auto first = trickplayFrame(info, 0);
  REQUIRE(first);
  CHECK(first->tileIndex == 0);
  CHECK(first->x == 0);
  CHECK(first->y == 0);

  // Thumbnail 123: tile 1, thumbnail 23 in it, row 2, column 3.
  auto mid = trickplayFrame(info, 1'235'000);
  REQUIRE(mid);
  CHECK(mid->tileIndex == 1);
  CHECK(mid->x == 3 * 320);
  CHECK(mid->y == 2 * 180);
  CHECK(mid->width == 320);

  // Past the end the last thumbnail is shown, before the start the first.
  auto end = trickplayFrame(info, 99'999'999);
  REQUIRE(end);
  CHECK(end->tileIndex == 2);
  CHECK(end->x == 9 * 320);
  CHECK(end->y == 4 * 180);
  CHECK(trickplayFrame(info, -5)->tileIndex == 0);

  CHECK_FALSE(trickplayFrame(TrickplayInfo{}, 0));
}

TEST_CASE("Home rows ask the routes Jellyfin Web uses") {
  Signed s([](const HttpRequest& r) {
    if (r.path.endsWith(QLatin1String("/Items/Latest")))
      return jfd::test::json(R"([{"Id": "s1", "Name": "Show", "Type": "Series",
                                  "UserData": {"UnplayedItemCount": 2}}])");
    return jfd::test::json(R"({"Items": [{"Id": "v1", "Name": "Shows", "Type": "CollectionFolder",
                                         "CollectionType": "tvshows"}], "TotalRecordCount": 1})");
  });

  auto views = run<ItemPage>([&](auto done) { s.client.views(nullptr, done); });
  REQUIRE(views.ok());
  CHECK(s.last().path == QStringLiteral("/jf/UserViews"));
  CHECK(s.param("userId") == QStringLiteral("user1"));
  CHECK(views->items[0].collectionType == QStringLiteral("tvshows"));

  REQUIRE(run<ItemPage>([&](auto done) { s.client.resumeItems(12, {}, nullptr, done); }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/UserItems/Resume"));
  CHECK(s.param("mediaTypes") == QStringLiteral("Video"));
  CHECK(s.param("limit") == QStringLiteral("12"));

  REQUIRE(run<ItemPage>([&](auto done) {
            s.client.nextUp(8, QStringLiteral("v1"), nullptr, done);
          }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/Shows/NextUp"));
  CHECK(s.param("parentId") == QStringLiteral("v1"));
  CHECK(s.param("enableResumable") == QStringLiteral("false"));

  auto latest = run<ItemPage>(
      [&](auto done) { s.client.latestItems(QStringLiteral("v1"), 16, nullptr, done); });
  REQUIRE(latest.ok());
  CHECK(s.last().path == QStringLiteral("/jf/Items/Latest"));
  REQUIRE(latest->items.size() == 1);
  CHECK(latest->items[0].userData.unplayedItemCount == 2);

  REQUIRE(run<ItemPage>([&](auto done) {
            s.client.similarItems(QStringLiteral("s1"), 6, nullptr, done);
          }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/Items/s1/Similar"));
}

TEST_CASE("Library listings send sorting, filters and paging") {
  Signed s([](const HttpRequest&) {
    return jfd::test::json(R"({"Items": [], "TotalRecordCount": 340, "StartIndex": 100})");
  });
  ItemQuery q;
  q.parentId = QStringLiteral("v1");
  q.includeItemTypes = {QStringLiteral("Movie")};
  q.sortBy = QStringLiteral("CommunityRating,SortName");
  q.descending = true;
  q.genres = {QStringLiteral("Drama"), QStringLiteral("Sci-Fi")};
  q.years = {1999, 2004};
  q.isPlayed = false;
  q.isFavorite = true;
  q.startIndex = 100;
  q.limit = 50;
  auto page = run<ItemPage>([&](auto done) { s.client.items(q, nullptr, done); });
  REQUIRE(page.ok());
  CHECK(page->totalRecordCount == 340);
  CHECK(page->startIndex == 100);
  CHECK(s.param("sortBy") == QStringLiteral("CommunityRating,SortName"));
  CHECK(s.param("sortOrder") == QStringLiteral("Descending"));
  CHECK(s.param("genres") == QStringLiteral("Drama|Sci-Fi"));
  CHECK(s.param("years") == QStringLiteral("1999,2004"));
  CHECK(s.param("isPlayed") == QStringLiteral("false"));
  CHECK(s.param("isFavorite") == QStringLiteral("true"));
  CHECK(s.param("startIndex") == QStringLiteral("100"));

  s.server.setHandler([](const HttpRequest&) {
    return jfd::test::json(
        R"({"Genres": ["Drama"], "Years": [1999, 2004], "OfficialRatings": ["R"], "Tags": []})");
  });
  auto filters = run<ItemFilters>([&](auto done) {
    s.client.itemFilters(QStringLiteral("v1"), {QStringLiteral("Movie")}, nullptr, done);
  });
  REQUIRE(filters.ok());
  CHECK(s.last().path == QStringLiteral("/jf/Items/Filters"));
  CHECK(filters->genres == QStringList{QStringLiteral("Drama")});
  CHECK(filters->years == QList<int>{1999, 2004});
}

TEST_CASE("Favorites are set and cleared and return the new user data") {
  Signed s([](const HttpRequest& r) {
    return jfd::test::json(r.method == "POST" ? R"({"IsFavorite": true, "ItemId": "e1"})"
                                              : R"({"IsFavorite": false, "ItemId": "e1"})");
  });
  auto on = run<UserItemData>(
      [&](auto done) { s.client.setFavorite(QStringLiteral("e1"), true, nullptr, done); });
  REQUIRE(on.ok());
  CHECK(s.last().method == QByteArray("POST"));
  CHECK(s.last().path == QStringLiteral("/jf/UserFavoriteItems/e1"));
  CHECK(on->isFavorite);
  CHECK(on->itemId == QStringLiteral("e1"));
  auto off = run<UserItemData>(
      [&](auto done) { s.client.setFavorite(QStringLiteral("e1"), false, nullptr, done); });
  REQUIRE(off.ok());
  CHECK(s.last().method == QByteArray("DELETE"));
  CHECK_FALSE(off->isFavorite);
}

TEST_CASE("Media segments are sorted and empty ones dropped") {
  Signed s([](const HttpRequest&) {
    return jfd::test::json(R"({"Items": [
      {"Id": "b", "ItemId": "e1", "Type": "Outro", "StartTicks": 2000000000, "EndTicks": 2300000000},
      {"Id": "a", "ItemId": "e1", "Type": 5, "StartTicks": 0, "EndTicks": 900000000},
      {"Id": "c", "ItemId": "e1", "Type": "Recap", "StartTicks": 50, "EndTicks": 50}]})");
  });
  auto segments = run<QList<MediaSegment>>(
      [&](auto done) { s.client.mediaSegments(QStringLiteral("e1"), nullptr, done); });
  REQUIRE(segments.ok());
  CHECK(s.last().path == QStringLiteral("/jf/MediaSegments/e1"));
  REQUIRE(segments->size() == 2);
  CHECK(segments->at(0).type == QStringLiteral("Intro"));
  CHECK(segments->at(1).type == QStringLiteral("Outro"));
  CHECK(segments->at(1).endTicks == 2300000000);
}

TEST_CASE("Image and trickplay URLs; authorized fetches stay on this server") {
  Signed s([](const HttpRequest& r) {
    return HttpResponse{200, "JPEG:" + r.headers.value("authorization"), "image/jpeg", 0};
  });
  ImageRequest image;
  image.itemId = QStringLiteral("s1");
  image.type = QStringLiteral("Backdrop");
  image.index = 1;
  image.tag = QStringLiteral("b2");
  image.fillWidth = 1280;
  image.fillHeight = 720;
  const QUrl imageUrl = s.client.imageUrl(image);
  CHECK(imageUrl.path() == QStringLiteral("/jf/Items/s1/Images/Backdrop/1"));
  const QUrlQuery imageQuery(imageUrl);
  CHECK(imageQuery.queryItemValue(QStringLiteral("tag")) == QStringLiteral("b2"));
  CHECK(imageQuery.queryItemValue(QStringLiteral("fillWidth")) == QStringLiteral("1280"));
  CHECK(imageQuery.queryItemValue(QStringLiteral("fillHeight")) == QStringLiteral("720"));
  CHECK_FALSE(imageQuery.hasQueryItem(QStringLiteral("ApiKey")));

  const QUrl tile = s.client.trickplayTileUrl(QStringLiteral("e1"), QStringLiteral("e1"), 320, 2);
  CHECK(tile.path() == QStringLiteral("/jf/Videos/e1/Trickplay/320/2.jpg"));
  CHECK(QUrlQuery(tile).queryItemValue(QStringLiteral("mediaSourceId")) == QStringLiteral("e1"));

  auto body = run<JellyfinClient::RawResponse>(
      [&](auto done) { s.client.authorizedGet(tile, nullptr, done); });
  REQUIRE(body.ok());
  CHECK(body->body.startsWith("JPEG:MediaBrowser"));
  CHECK(body->body.contains("Token=\"token1\""));

  auto elsewhere = run<JellyfinClient::RawResponse>([&](auto done) {
    s.client.authorizedGet(QUrl(QStringLiteral("http://example.org/x.jpg")), nullptr, done);
  });
  REQUIRE_FALSE(elsewhere.ok());
  CHECK(elsewhere.error().kind == ApiError::Kind::Unsupported);
  CHECK(s.server.requests().size() == 1);
}

TEST_CASE("Discovery replies are parsed and each server is reported once") {
  CHECK_FALSE(ServerDiscovery::parseReply("nonsense"));
  CHECK_FALSE(ServerDiscovery::parseReply(R"({"Id": "x"})"));
  auto parsed = ServerDiscovery::parseReply(
      R"({"Address": "http://192.168.1.5:8096/", "Id": "abc", "Name": "Home", "EndpointAddress": null})");
  REQUIRE(parsed);
  CHECK(parsed->address.toString() == QStringLiteral("http://192.168.1.5:8096"));
  CHECK(parsed->name == QStringLiteral("Home"));

  // Two responders standing in for the same server on two interfaces and
  // another server.
  QUdpSocket responder;
  REQUIRE(responder.bind(QHostAddress::LocalHost, 0));
  QList<QByteArray> questions;
  QObject::connect(&responder, &QUdpSocket::readyRead, [&]() {
    while (responder.hasPendingDatagrams()) {
      const QNetworkDatagram d = responder.receiveDatagram();
      questions.append(d.data());
      const QByteArray a = R"({"Address": "http://10.0.0.2:8096", "Id": "one", "Name": "One"})";
      const QByteArray b = R"({"Address": "https://media.lan", "Id": "two", "Name": "Two"})";
      responder.writeDatagram(d.makeReply(a));
      responder.writeDatagram(d.makeReply(a));
      responder.writeDatagram(d.makeReply("garbage"));
      responder.writeDatagram(d.makeReply(b));
    }
  });

  ServerDiscovery discovery;
  discovery.setTargets({{QHostAddress::LocalHost, responder.localPort()}});
  QSignalSpy found(&discovery, &ServerDiscovery::serverFound);
  QSignalSpy finished(&discovery, &ServerDiscovery::finished);
  discovery.start(400);
  CHECK(discovery.isRunning());
  REQUIRE(waitFor([&] { return finished.count() == 1; }));
  CHECK_FALSE(discovery.isRunning());
  REQUIRE(questions.size() == 1);
  CHECK(questions[0] == QByteArray("who is JellyfinServer?"));
  CHECK(found.count() == 2);
  REQUIRE(discovery.servers().size() == 2);
  CHECK(discovery.servers()[0].id == QStringLiteral("one"));
  CHECK(discovery.servers()[1].address.toString() == QStringLiteral("https://media.lan"));
}
