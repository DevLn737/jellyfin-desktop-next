#include "../support/DoctestQt.h"

#include "../support/FakeServer.h"

#include <jfd/api/LiveTv.h>

#include <QJsonDocument>

#include <algorithm>
#include <optional>

// The LiveTv API on fake answers shaped like Jellyfin 12.2's (taken from the
// extras stand: an M3U tuner with an XMLTV guide).

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
    LiveTvApi liveTv{client};

    explicit Signed(FakeServer::Handler handler) : server(std::move(handler)) {
      client.setServerUrl(server.url(QStringLiteral("/jf")));
      client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
    }
    const HttpRequest& last() const { return server.requests().last(); }
    QString param(const char* name) const { return last().query.queryItemValue(QString::fromLatin1(name)); }
  };

  QDateTime utc(const char* text) {
    QDateTime at = QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
    REQUIRE(at.isValid());
    return at.toUTC();
  }

  const char* ChannelsJson = R"({"Items": [
    {"Name": "Test Channel 1", "Id": "c1", "Number": "1", "ChannelNumber": "1", "Type": "TvChannel",
     "MediaType": "Video", "ChannelType": "TV", "ImageTags": {"Primary": "logo1"},
     "UserData": {"IsFavorite": true, "Played": false},
     "CurrentProgram": {"Name": "Channel 1 at 16:30", "Id": "p1", "ChannelId": "c1", "Type": "Program",
       "RunTimeTicks": 18000000000, "StartDate": "2026-11-10T16:30:00.0000000Z",
       "EndDate": "2026-11-10T17:00:00.0000000Z"}},
    {"Name": "Test Channel 2", "Id": "c2", "Number": "2", "ChannelNumber": "2", "Type": "TvChannel",
     "ImageTags": {}, "UserData": {"IsFavorite": false}},
    {"Name": "Undated", "Id": "c3", "Type": "TvChannel",
     "CurrentProgram": {"Name": "No dates", "Id": "p9"}}
  ], "TotalRecordCount": 50, "StartIndex": 0})";
} // namespace

TEST_CASE("Live TV info: available when a service works and the user is enabled") {
  Signed s([](const HttpRequest&) {
    return jfd::test::json(R"({"Services": [{"Name": "Emby", "Status": "Ok", "HasUpdateAvailable": false,
      "IsVisible": false, "Tuners": []}], "IsEnabled": true,
      "EnabledUsers": ["5526b50ce00844fbb55cbe5c12afd6b2"]})");
  });
  auto info = run<LiveTvInfo>([&](auto done) { s.liveTv.info(nullptr, done); });
  REQUIRE(info.ok());
  CHECK(s.last().method == QByteArray("GET"));
  CHECK(s.last().path == QStringLiteral("/jf/LiveTv/Info"));
  CHECK(info->isEnabled);
  REQUIRE(info->services.size() == 1);
  CHECK(info->services[0].status == QStringLiteral("Ok"));
  CHECK_FALSE(info->services[0].isVisible);
  // Ids compare without dashes and case.
  CHECK(info->availableTo(QStringLiteral("5526B50C-E008-44FB-B55C-BE5C12AFD6B2")));
  CHECK_FALSE(info->availableTo(QStringLiteral("someone-else")));

  LiveTvInfo open = *info;
  open.enabledUsers.clear();
  CHECK(open.availableTo(QStringLiteral("anyone")));
  open.services[0].status = QStringLiteral("Unavailable");
  CHECK_FALSE(open.availableTo(QStringLiteral("anyone")));
  open.services[0].status = QStringLiteral("Ok");
  open.isEnabled = false;
  CHECK_FALSE(open.availableTo(QStringLiteral("anyone")));
  CHECK_FALSE(LiveTvInfo{}.availableTo(QStringLiteral("anyone")));
}

TEST_CASE("Channels come with numbers, logos, favorites and what is on now") {
  Signed s([](const HttpRequest&) { return jfd::test::json(ChannelsJson); });
  LiveTvChannelQuery query;
  query.startIndex = 100;
  query.limit = 50;
  auto page = run<LiveTvChannelPage>([&](auto done) { s.liveTv.channels(query, nullptr, done); });
  REQUIRE(page.ok());
  CHECK(s.last().path == QStringLiteral("/jf/LiveTv/Channels"));
  CHECK(s.param("userId") == QStringLiteral("user1"));
  CHECK(s.param("startIndex") == QStringLiteral("100"));
  CHECK(s.param("limit") == QStringLiteral("50"));
  CHECK(s.param("addCurrentProgram") == QStringLiteral("true"));
  CHECK(s.param("enableUserData") == QStringLiteral("true"));
  CHECK_FALSE(s.last().query.hasQueryItem(QStringLiteral("isFavorite")));
  CHECK(page->totalRecordCount == 50);
  REQUIRE(page->channels.size() == 3);

  const LiveTvChannel& one = page->channels[0];
  CHECK(one.id == QStringLiteral("c1"));
  CHECK(one.name == QStringLiteral("Test Channel 1"));
  CHECK(one.number == QStringLiteral("1"));
  CHECK(one.primaryImageTag == QStringLiteral("logo1"));
  CHECK(one.isFavorite);
  REQUIRE(one.currentProgram.has_value());
  CHECK(one.currentProgram->name == QStringLiteral("Channel 1 at 16:30"));
  CHECK(one.currentProgram->start == utc("2026-11-10T16:30:00Z"));
  CHECK(one.currentProgram->end == utc("2026-11-10T17:00:00Z"));
  CHECK(one.currentProgram->start.offsetFromUtc() == 0);

  CHECK_FALSE(page->channels[1].currentProgram.has_value());
  CHECK(page->channels[1].primaryImageTag.isEmpty());
  // A programme without dates is dropped, the channel stays.
  CHECK(page->channels[2].name == QStringLiteral("Undated"));
  CHECK_FALSE(page->channels[2].currentProgram.has_value());
  CHECK(page->channels[2].number.isEmpty());
}

TEST_CASE("Favorite channels only, when asked") {
  Signed s([](const HttpRequest&) { return jfd::test::json(R"({"Items": [], "TotalRecordCount": 0})"); });
  LiveTvChannelQuery query;
  query.isFavorite = true;
  query.addCurrentProgram = false;
  REQUIRE(run<LiveTvChannelPage>([&](auto done) { s.liveTv.channels(query, nullptr, done); }).ok());
  CHECK(s.param("isFavorite") == QStringLiteral("true"));
  CHECK(s.param("addCurrentProgram") == QStringLiteral("false"));
}

TEST_CASE("Programmes are asked for a time window and sorted by start") {
  Signed s([](const HttpRequest&) {
    return jfd::test::json(R"({"Items": [
      {"Name": "Channel 1 at 15:30", "Id": "p0", "ChannelId": "c1", "Type": "Program",
       "EpisodeTitle": "Pilot", "Overview": "Test programme.", "IsLive": true, "IsNews": true,
       "StartDate": "2026-11-10T15:30:00.0000000Z", "EndDate": "2026-11-10T16:00:00.0000000Z"},
      {"Name": "Zoneless", "Id": "p1", "ChannelId": "c1",
       "StartDate": "2026-11-10T16:00:00", "EndDate": "2026-11-10T16:30:00"},
      {"Name": "Broken", "Id": "p2", "ChannelId": "c1", "StartDate": "nonsense"}
    ], "TotalRecordCount": 2450, "StartIndex": 500})");
  });
  LiveTvProgramQuery query;
  query.channelIds = {QStringLiteral("c1"), QStringLiteral("c2")};
  query.from = utc("2026-11-10T16:10:00Z");
  query.to = utc("2026-11-10T19:10:00Z");
  query.startIndex = 500;
  query.limit = 500;
  auto page = run<LiveTvProgramPage>([&](auto done) { s.liveTv.programs(query, nullptr, done); });
  REQUIRE(page.ok());
  CHECK(s.last().path == QStringLiteral("/jf/LiveTv/Programs"));
  CHECK(s.param("channelIds") == QStringLiteral("c1,c2"));
  // Programmes that end after the start and begin before the end.
  CHECK(s.param("minEndDate") == QStringLiteral("2026-11-10T16:10:00.000Z"));
  CHECK(s.param("maxStartDate") == QStringLiteral("2026-11-10T19:10:00.000Z"));
  CHECK(s.param("sortBy") == QStringLiteral("StartDate"));
  CHECK(s.param("startIndex") == QStringLiteral("500"));
  CHECK(s.param("enableTotalRecordCount") == QStringLiteral("true"));
  CHECK(page->totalRecordCount == 2450);
  CHECK(page->startIndex == 500);
  // The undated one is left out.
  REQUIRE(page->programs.size() == 2);
  const LiveTvProgram& first = page->programs[0];
  CHECK(first.episodeTitle == QStringLiteral("Pilot"));
  CHECK(first.overview == QStringLiteral("Test programme."));
  CHECK(first.isLive);
  CHECK(first.isNews);
  CHECK_FALSE(first.isMovie);
  // A date without a zone is read as UTC.
  CHECK(page->programs[1].start == utc("2026-11-10T16:00:00Z"));
}

TEST_CASE("A programme's progress and whether it is on") {
  LiveTvProgram p;
  p.start = utc("2026-11-10T16:00:00Z");
  p.end = utc("2026-11-10T16:30:00Z");
  CHECK(p.isOnAt(utc("2026-11-10T16:00:00Z")));
  CHECK(p.isOnAt(utc("2026-11-10T16:29:59Z")));
  CHECK_FALSE(p.isOnAt(utc("2026-11-10T16:30:00Z")));
  CHECK_FALSE(p.isOnAt(utc("2026-11-10T15:59:59Z")));
  CHECK(p.progressAt(utc("2026-11-10T16:15:00Z")) == doctest::Approx(0.5));
  CHECK(p.progressAt(utc("2026-11-10T15:00:00Z")) == doctest::Approx(0.0));
  CHECK(p.progressAt(utc("2026-11-10T18:00:00Z")) == doctest::Approx(1.0));
  LiveTvProgram undated;
  CHECK_FALSE(undated.isOnAt(utc("2026-11-10T16:15:00Z")));
  CHECK(undated.progressAt(utc("2026-11-10T16:15:00Z")) == doctest::Approx(0.0));
}

TEST_CASE("Closing a live stream names it") {
  Signed s([](const HttpRequest&) { return HttpResponse{204, {}, {}, 0}; });
  auto closed = run<Empty>([&](auto done) {
    s.liveTv.closeLiveStream(QStringLiteral("tuner_stream_1"), nullptr, done);
  });
  REQUIRE(closed.ok());
  CHECK(s.last().method == QByteArray("POST"));
  CHECK(s.last().path == QStringLiteral("/jf/LiveStreams/Close"));
  CHECK(s.param("liveStreamId") == QStringLiteral("tuner_stream_1"));
}

TEST_CASE("A channel list without Items is an invalid answer") {
  Signed s([](const HttpRequest&) { return jfd::test::json(R"({"TotalRecordCount": 1})"); });
  auto page = run<LiveTvChannelPage>([&](auto done) { s.liveTv.channels({}, nullptr, done); });
  REQUIRE_FALSE(page.ok());
  CHECK(page.error().kind == ApiError::Kind::InvalidResponse);
}

TEST_CASE("Every channel, page after page") {
  Signed s([](const HttpRequest& r) {
    const int start = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
    QByteArray items;
    const int limit = r.query.queryItemValue(QStringLiteral("limit")).toInt();
    for (int i = start; i < std::min(5, start + limit); ++i) {
      if (!items.isEmpty()) items += ",";
      items += "{\"Id\": \"c" + QByteArray::number(i) + "\", \"Name\": \"Channel\"}";
    }
    return HttpResponse{200, "{\"Items\": [" + items + "], \"TotalRecordCount\": 5, \"StartIndex\": " +
                                 QByteArray::number(start) + "}",
                        "application/json", 0};
  });
  LiveTvChannelQuery query;
  query.limit = 2;
  auto all = run<QList<LiveTvChannel>>([&](auto done) { s.liveTv.allChannels(query, nullptr, done); });
  REQUIRE(all.ok());
  REQUIRE(all->size() == 5);
  CHECK(all->at(4).id == QStringLiteral("c4"));
  CHECK(s.server.requests().size() == 3);
}

TEST_CASE("One channel by id, and a whole guide window over pages, by start") {
  Signed s([](const HttpRequest& r) {
    if (r.path == QStringLiteral("/jf/LiveTv/Channels/c1"))
      return HttpResponse{200, R"({"Id": "c1", "Name": "Test Channel 1", "ChannelNumber": "1"})",
                          "application/json", 0};
    if (r.path == QStringLiteral("/jf/LiveTv/Programs")) {
      const int start = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
      if (start == 0)
        return HttpResponse{200, R"({"Items": [
          {"Id": "b", "ChannelId": "c1", "Name": "B", "StartDate": "2026-11-10T17:00:00Z", "EndDate": "2026-11-10T17:30:00Z"},
          {"Id": "x", "ChannelId": "c1", "Name": "No dates"}], "TotalRecordCount": 3, "StartIndex": 0})",
                            "application/json", 0};
      return HttpResponse{200, R"({"Items": [
          {"Id": "a", "ChannelId": "c1", "Name": "A", "StartDate": "2026-11-10T16:30:00Z", "EndDate": "2026-11-10T17:00:00Z"}],
          "TotalRecordCount": 3, "StartIndex": 2})",
                          "application/json", 0};
    }
    return HttpResponse{404, {}, {}, 0};
  });
  auto channel = run<LiveTvChannel>([&](auto done) { s.liveTv.channel(QStringLiteral("c1"), nullptr, done); });
  REQUIRE(channel.ok());
  CHECK(channel->name == QStringLiteral("Test Channel 1"));
  CHECK(s.param("userId") == QStringLiteral("user1"));

  LiveTvProgramQuery query;
  query.channelIds = {QStringLiteral("c1")};
  query.from = utc("2026-11-10T16:00:00Z");
  query.to = utc("2026-11-10T22:00:00Z");
  query.limit = 2;
  auto all = run<QList<LiveTvProgram>>([&](auto done) { s.liveTv.allPrograms(query, nullptr, done); });
  REQUIRE(all.ok());
  REQUIRE(all->size() == 2);
  CHECK(all->at(0).id == QStringLiteral("a"));
  CHECK(all->at(1).id == QStringLiteral("b"));
  CHECK(s.param("startIndex") == QStringLiteral("2"));
  CHECK(s.param("channelIds") == QStringLiteral("c1"));
}
