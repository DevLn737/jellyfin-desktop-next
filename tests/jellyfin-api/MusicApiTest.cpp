#include "../support/DoctestQt.h"

#include "../support/FakeServer.h"

#include <jfd/api/MusicApi.h>

#include <QJsonArray>
#include <QJsonDocument>

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
    MusicApi music{client};

    explicit Signed(FakeServer::Handler handler) : server(std::move(handler)) {
      client.setServerUrl(server.url(QStringLiteral("/jf")));
      client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
    }
    const HttpRequest& last() const { return server.requests().last(); }
    QString param(const char* name) const { return last().query.queryItemValue(QString::fromLatin1(name)); }
  };

  const char* TrackJson = R"({
    "Id": "t1", "Name": "Alpha One", "Type": "Audio", "MediaType": "Audio",
    "Album": "First Album", "AlbumId": "a1", "AlbumArtist": "Alpha Artist",
    "AlbumPrimaryImageTag": "ap1", "Artists": ["Alpha Artist", "Guest"],
    "ArtistItems": [{"Id": "r1", "Name": "Alpha Artist"}, {"Id": "r2", "Name": "Guest"}],
    "AlbumArtists": [{"Id": "r1", "Name": "Alpha Artist"}],
    "IndexNumber": 1, "RunTimeTicks": 200000000, "NormalizationGain": -6.5,
    "PlaylistItemId": "pl-entry-1"
  })";
} // namespace

TEST_CASE("Tracks carry their album, artists and gain") {
  Signed s([](const HttpRequest&) {
    return jfd::test::json(QByteArray(R"({"Items": [)") + TrackJson + R"(], "TotalRecordCount": 1})");
  });
  auto page = run<ItemPage>([&](auto done) { s.music.albumTracks(QStringLiteral("a1"), nullptr, done); });
  REQUIRE(page.ok());
  CHECK(s.last().path == QStringLiteral("/jf/Items"));
  CHECK(s.param("parentId") == QStringLiteral("a1"));
  CHECK(s.param("includeItemTypes") == QStringLiteral("Audio"));
  CHECK(s.param("sortBy") == QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
  REQUIRE(page->items.size() == 1);
  const Item& track = page->items.front();
  CHECK(track.album == QStringLiteral("First Album"));
  CHECK(track.albumId == QStringLiteral("a1"));
  CHECK(track.albumArtist == QStringLiteral("Alpha Artist"));
  CHECK(track.albumPrimaryImageTag == QStringLiteral("ap1"));
  CHECK(track.artists == QStringList{QStringLiteral("Alpha Artist"), QStringLiteral("Guest")});
  REQUIRE(track.artistItems.size() == 2);
  CHECK(track.artistItems[1].id == QStringLiteral("r2"));
  CHECK(track.albumArtists.size() == 1);
  CHECK(track.normalizationGain == doctest::Approx(-6.5));
  CHECK(track.playlistItemId == QStringLiteral("pl-entry-1"));
}

TEST_CASE("Music lists ask for the right types, sorting and artist filters") {
  Signed s([](const HttpRequest&) {
    return jfd::test::json(R"({"Items": [], "TotalRecordCount": 12000, "StartIndex": 200})");
  });
  MusicQuery q;
  q.parentId = QStringLiteral("lib");
  q.sort = MusicSort::DateAdded;
  q.descending = true;
  q.startIndex = 200;
  q.limit = 100;
  q.albumArtistIds = {QStringLiteral("r1")};
  q.isFavorite = true;

  auto albums = run<ItemPage>([&](auto done) { s.music.albums(q, nullptr, done); });
  REQUIRE(albums.ok());
  CHECK(albums->totalRecordCount == 12000);
  CHECK(s.last().path == QStringLiteral("/jf/Items"));
  CHECK(s.param("includeItemTypes") == QStringLiteral("MusicAlbum"));
  CHECK(s.param("sortBy") == QStringLiteral("DateCreated"));
  CHECK(s.param("sortOrder") == QStringLiteral("Descending"));
  CHECK(s.param("albumArtistIds") == QStringLiteral("r1"));
  CHECK(s.param("isFavorite") == QStringLiteral("true"));
  CHECK(s.param("startIndex") == QStringLiteral("200"));
  CHECK(s.param("userId") == QStringLiteral("user1"));

  REQUIRE(run<ItemPage>([&](auto done) { s.music.songs(q, nullptr, done); }).ok());
  CHECK(s.param("includeItemTypes") == QStringLiteral("Audio"));

  REQUIRE(run<ItemPage>([&](auto done) { s.music.artists(q, false, nullptr, done); }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/Artists/AlbumArtists"));
  REQUIRE(run<ItemPage>([&](auto done) { s.music.artists(q, true, nullptr, done); }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/Artists"));

  REQUIRE(run<ItemPage>([&](auto done) { s.music.genres(q, nullptr, done); }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/MusicGenres"));

  REQUIRE(run<ItemPage>([&](auto done) { s.music.playlists(q, nullptr, done); }).ok());
  CHECK(s.param("includeItemTypes") == QStringLiteral("Playlist"));
  CHECK(s.param("mediaTypes") == QStringLiteral("Audio"));

  REQUIRE(run<ItemPage>([&](auto done) { s.music.playlistTracks(QStringLiteral("p1"), nullptr, done); }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/Playlists/p1/Items"));

  REQUIRE(run<ItemPage>([&](auto done) { s.music.recentlyPlayed(QStringLiteral("lib"), 20, nullptr, done); }).ok());
  CHECK(s.param("sortBy") == QStringLiteral("DatePlayed"));
  CHECK(s.param("filters") == QStringLiteral("IsPlayed"));
}

TEST_CASE("Latest albums are a bare array") {
  Signed s([](const HttpRequest&) { return jfd::test::json(QByteArray("[") + TrackJson + "]"); });
  auto page = run<ItemPage>([&](auto done) { s.music.latestAlbums(QStringLiteral("lib"), 16, nullptr, done); });
  REQUIRE(page.ok());
  CHECK(s.last().path == QStringLiteral("/jf/Items/Latest"));
  CHECK(s.param("includeItemTypes") == QStringLiteral("MusicAlbum"));
  CHECK(page->items.size() == 1);
}

TEST_CASE("Playlists are created, added to and trimmed") {
  Signed s([](const HttpRequest& request) {
    if (request.method == "POST" && request.path == QStringLiteral("/jf/Playlists"))
      return jfd::test::json(R"({"Id": "newlist"})");
    return HttpResponse{204, {}, {}, 0};
  });
  auto created = run<QString>([&](auto done) {
    s.music.createPlaylist(QStringLiteral("Mix"), {QStringLiteral("t1"), QStringLiteral("t2")}, nullptr, done);
  });
  REQUIRE(created.ok());
  CHECK(*created == QStringLiteral("newlist"));
  const QJsonObject body = QJsonDocument::fromJson(s.last().body).object();
  CHECK(body.value(QLatin1String("Name")).toString() == QStringLiteral("Mix"));
  CHECK(body.value(QLatin1String("MediaType")).toString() == QStringLiteral("Audio"));
  CHECK(body.value(QLatin1String("Ids")).toArray().size() == 2);
  CHECK(body.value(QLatin1String("UserId")).toString() == QStringLiteral("user1"));

  REQUIRE(run<Empty>([&](auto done) {
            s.music.addToPlaylist(QStringLiteral("newlist"), {QStringLiteral("t3")}, nullptr, done);
          }).ok());
  CHECK(s.last().path == QStringLiteral("/jf/Playlists/newlist/Items"));
  CHECK(s.param("ids") == QStringLiteral("t3"));

  REQUIRE(run<Empty>([&](auto done) {
            s.music.removeFromPlaylist(QStringLiteral("newlist"), {QStringLiteral("e1"), QStringLiteral("e2")},
                                       nullptr, done);
          }).ok());
  CHECK(s.last().method == "DELETE");
  CHECK(s.param("entryIds") == QStringLiteral("e1,e2"));
}
