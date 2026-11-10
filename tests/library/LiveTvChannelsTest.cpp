#include "LibraryTestSupport.h"

#include <jfd/library/LiveTvChannels.h>

#include <QSignalSpy>

// The Live TV channel list on a fake server: channels page by page, now and
// next from the guide window, the rows moving on with the clock, and the
// availability that drives the sidebar entry.

using namespace jfd;
using jfd::library::LiveTvChannels;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::LibraryFixture;
using jfd::test::ok;
using jfd::test::waitFor;

namespace {
  QDateTime utc(const char* text) {
    QDateTime at = QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate);
    REQUIRE(at.isValid());
    return at.toUTC();
  }

  QByteArray channel(int n, const char* currentName = nullptr) {
    QByteArray json = "{\"Id\": \"c" + QByteArray::number(n) + "\", \"Name\": \"Test Channel " +
                      QByteArray::number(n) + "\", \"ChannelNumber\": \"" + QByteArray::number(n) +
                      "\", \"Type\": \"TvChannel\"";
    if (n == 1) json += ", \"ImageTags\": {\"Primary\": \"logo\"}";
    if (currentName)
      json += ", \"CurrentProgram\": {\"Id\": \"cur" + QByteArray::number(n) + "\", \"Name\": \"" +
              currentName +
              "\", \"StartDate\": \"2026-11-10T16:00:00.0000000Z\", \"EndDate\": "
              "\"2026-11-10T16:30:00.0000000Z\"}";
    return json + "}";
  }

  QByteArray program(const char* id, int channelNumber, const char* name, const char* start,
                     const char* end) {
    return QByteArray("{\"Id\": \"") + id + "\", \"ChannelId\": \"c" + QByteArray::number(channelNumber) +
           "\", \"Name\": \"" + name + "\", \"StartDate\": \"" + start + "\", \"EndDate\": \"" + end + "\"}";
  }

  // Three channels, two per page; a guide for channels 1 and 2 over two
  // pages; channel 3 has none.
  HttpResponse stand(const HttpRequest& r) {
    if (r.path == QStringLiteral("/LiveTv/Channels")) {
      const int start = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
      if (start == 0) return ok(jfd::test::page(channel(1) + "," + channel(2, "From the channel list"), 3, 0));
      return ok(jfd::test::page(channel(3), 3, 2));
    }
    if (r.path == QStringLiteral("/LiveTv/Programs")) {
      const int start = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
      if (start == 0)
        return ok(jfd::test::page(
            program("a2", 1, "Later on one", "2026-11-10T17:00:00Z", "2026-11-10T17:30:00Z") + "," +
                program("a1", 1, "Next on one", "2026-11-10T16:30:00Z", "2026-11-10T17:00:00Z"),
            4, 0));
      return ok(jfd::test::page(
          program("a0", 1, "Now on one", "2026-11-10T16:00:00Z", "2026-11-10T16:30:00Z") + "," +
              program("b1", 2, "Next on two", "2026-11-10T16:30:00Z", "2026-11-10T17:00:00Z"),
          4, 2));
    }
    if (r.path == QStringLiteral("/LiveTv/Info"))
      return ok(R"({"Services": [{"Name": "Emby", "Status": "Ok"}], "IsEnabled": true, "EnabledUsers": ["user1"]})");
    return HttpResponse{404, {}, {}, 0};
  }

  struct Fixture {
    LibraryFixture f{stand};
    QDateTime clock = utc("2026-11-10T16:15:00Z");
    LiveTvChannels model;

    static LiveTvChannels::Options options(QDateTime* clock) {
      LiveTvChannels::Options o;
      o.channelPageSize = 2;
      o.programPageSize = 2;
      o.now = [clock] { return *clock; };
      return o;
    }

    Fixture() : model(f.client, options(&clock)) {}

    QVariant at(int row, LiveTvChannels::Role role) const { return model.data(model.index(row), role); }

    void load() {
      model.reload();
      REQUIRE(model.loading());
      REQUIRE(waitFor([&] { return !model.loading(); }));
    }
  };
} // namespace

TEST_CASE("Channels list with now and next from the guide, in server order") {
  Fixture t;
  QSignalSpy counted(&t.model, &LiveTvChannels::countChanged);
  t.load();
  CHECK(t.model.errorText().isEmpty());
  REQUIRE(t.model.count() == 3);
  CHECK(counted.count() == 1);
  CHECK(t.f.count(QStringLiteral("/LiveTv/Channels")) == 2);
  CHECK(t.f.count(QStringLiteral("/LiveTv/Programs")) == 2);

  // The guide window starts now and runs three hours.
  const HttpRequest* guide = nullptr;
  for (const HttpRequest& r : t.f.server.requests())
    if (r.path == QStringLiteral("/LiveTv/Programs")) guide = &r;
  REQUIRE(guide != nullptr);
  CHECK(guide->query.queryItemValue(QStringLiteral("minEndDate")) == QStringLiteral("2026-11-10T16:15:00.000Z"));
  CHECK(guide->query.queryItemValue(QStringLiteral("maxStartDate")) == QStringLiteral("2026-11-10T19:15:00.000Z"));

  CHECK(t.at(0, LiveTvChannels::NameRole).toString() == QStringLiteral("Test Channel 1"));
  CHECK(t.at(0, LiveTvChannels::IdRole).toString() == QStringLiteral("c1"));
  CHECK(t.at(0, LiveTvChannels::NumberRole).toString() == QStringLiteral("1"));
  CHECK(t.at(0, LiveTvChannels::ImageUrlRole).toUrl().path() == QStringLiteral("/Items/c1/Images/Primary"));
  CHECK(t.at(1, LiveTvChannels::ImageUrlRole).toUrl().isEmpty());

  CHECK(t.at(0, LiveTvChannels::NowTitleRole).toString() == QStringLiteral("Now on one"));
  CHECK(t.at(0, LiveTvChannels::NowProgressRole).toDouble() == doctest::Approx(0.5));
  CHECK(t.at(0, LiveTvChannels::NowStartRole).toDateTime() == utc("2026-11-10T16:00:00Z"));
  CHECK(t.at(0, LiveTvChannels::NowEndRole).toDateTime() == utc("2026-11-10T16:30:00Z"));
  // Sorted by start, whatever order the pages came in.
  CHECK(t.at(0, LiveTvChannels::NextTitleRole).toString() == QStringLiteral("Next on one"));
  CHECK(t.at(0, LiveTvChannels::NextStartRole).toDateTime() == utc("2026-11-10T16:30:00Z"));

  // What is on now comes from the channel list when the window has nothing.
  CHECK(t.at(1, LiveTvChannels::NowTitleRole).toString() == QStringLiteral("From the channel list"));
  CHECK(t.at(1, LiveTvChannels::NextTitleRole).toString() == QStringLiteral("Next on two"));

  // A channel the guide does not know still lists.
  CHECK(t.at(2, LiveTvChannels::NameRole).toString() == QStringLiteral("Test Channel 3"));
  CHECK(t.at(2, LiveTvChannels::NowTitleRole).toString().isEmpty());
  CHECK(t.at(2, LiveTvChannels::NowProgressRole).toDouble() == doctest::Approx(0.0));
  CHECK(t.at(2, LiveTvChannels::NextTitleRole).toString().isEmpty());
  CHECK_FALSE(t.at(2, LiveTvChannels::NextStartRole).isValid());

  const QHash<int, QByteArray> roles = t.model.roleNames();
  CHECK(roles.value(LiveTvChannels::IdRole) == QByteArray("channelId"));
  CHECK(roles.value(LiveTvChannels::NowProgressRole) == QByteArray("nowProgress"));
}

TEST_CASE("Rows move on with the clock") {
  Fixture t;
  t.load();
  QSignalSpy changed(&t.model, &QAbstractItemModel::dataChanged);
  t.clock = utc("2026-11-10T16:45:00Z");
  t.model.tick();
  CHECK(changed.count() == 1);
  CHECK(t.at(0, LiveTvChannels::NowTitleRole).toString() == QStringLiteral("Next on one"));
  CHECK(t.at(0, LiveTvChannels::NowProgressRole).toDouble() == doctest::Approx(0.5));
  CHECK(t.at(0, LiveTvChannels::NextTitleRole).toString() == QStringLiteral("Later on one"));
  // The channel list's programme is over; the guide has the next one.
  CHECK(t.at(1, LiveTvChannels::NowTitleRole).toString() == QStringLiteral("Next on two"));
  CHECK(t.at(1, LiveTvChannels::NextTitleRole).toString().isEmpty());
  // No new request while the window lasts.
  CHECK(t.f.count(QStringLiteral("/LiveTv/Programs")) == 2);
}

TEST_CASE("The guide is fetched again before its window runs out") {
  Fixture t;
  t.load();
  t.clock = utc("2026-11-10T18:30:00Z");
  t.model.tick();
  CHECK(t.model.loading());
  REQUIRE(waitFor([&] { return !t.model.loading(); }));
  CHECK(t.f.count(QStringLiteral("/LiveTv/Channels")) == 4);
  CHECK(t.f.count(QStringLiteral("/LiveTv/Programs")) == 4);
}

TEST_CASE("Without a guide the channels still list; without channels it is an error") {
  SUBCASE("guide fails") {
    LibraryFixture f([](const HttpRequest& r) {
      if (r.path == QStringLiteral("/LiveTv/Channels")) return ok(jfd::test::page(channel(5, "Only this"), 1));
      return HttpResponse{500, {}, {}, 0};
    });
    QDateTime clock = utc("2026-11-10T16:15:00Z");
    LiveTvChannels::Options o;
    o.now = [&clock] { return clock; };
    LiveTvChannels model(f.client, o);
    model.reload();
    REQUIRE(waitFor([&] { return !model.loading(); }));
    CHECK(model.errorText().isEmpty());
    REQUIRE(model.count() == 1);
    CHECK(model.data(model.index(0), LiveTvChannels::NowTitleRole).toString() == QStringLiteral("Only this"));
  }
  SUBCASE("channels fail") {
    LibraryFixture f([](const HttpRequest&) { return HttpResponse{500, {}, {}, 0}; });
    LiveTvChannels model(f.client);
    model.reload();
    REQUIRE(waitFor([&] { return !model.loading(); }));
    CHECK_FALSE(model.errorText().isEmpty());
    CHECK(model.count() == 0);
  }
  SUBCASE("no channels: no guide request") {
    LibraryFixture f([](const HttpRequest& r) {
      if (r.path == QStringLiteral("/LiveTv/Channels")) return ok(jfd::test::page({}, 0));
      return HttpResponse{500, {}, {}, 0};
    });
    LiveTvChannels model(f.client);
    model.reload();
    REQUIRE(waitFor([&] { return !model.loading(); }));
    CHECK(model.errorText().isEmpty());
    CHECK(model.count() == 0);
    CHECK(f.count(QStringLiteral("/LiveTv/Programs")) == 0);
  }
}

TEST_CASE("Availability follows LiveTv/Info; signing out clears it") {
  Fixture t;
  QSignalSpy spy(&t.model, &LiveTvChannels::availableChanged);
  CHECK_FALSE(t.model.available());
  t.model.checkAvailable();
  REQUIRE(waitFor([&] { return t.model.available(); }));
  CHECK(spy.count() == 1);
  t.load();
  REQUIRE(t.model.count() == 3);
  t.model.clear();
  CHECK_FALSE(t.model.available());
  CHECK(t.model.count() == 0);
  CHECK(spy.count() == 2);

  SUBCASE("a server without Live TV, or one that fails") {
    LibraryFixture off([](const HttpRequest& r) {
      if (r.path == QStringLiteral("/LiveTv/Info"))
        return ok(R"({"Services": [], "IsEnabled": false, "EnabledUsers": []})");
      return HttpResponse{500, {}, {}, 0};
    });
    LiveTvChannels model(off.client);
    model.checkAvailable();
    REQUIRE(waitFor([&] { return off.count(QStringLiteral("/LiveTv/Info")) == 1; }));
    test::waitFor([] { return false; }, 50);
    CHECK_FALSE(model.available());
  }
}

TEST_CASE("An answer to an older load is dropped") {
  Fixture t;
  t.model.reload();
  t.model.clear();
  test::waitFor([] { return false; }, 200);
  CHECK(t.model.count() == 0);
  CHECK_FALSE(t.model.loading());
}
