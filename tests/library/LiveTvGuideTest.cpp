#include "LibraryTestSupport.h"

#include <jfd/library/ItemListModel.h>
#include <jfd/library/LiveTvGuide.h>
#include <jfd/library/LiveTvOnNow.h>
#include <jfd/library/LiveTvSchedule.h>

#include <QSignalSpy>

#include <algorithm>

// The programme guide grid's model, the home screen's "On now" row and a
// channel's page on a fake server: the guide loads in chunks as the view
// reaches them, a row lists only the cells in view, and the focus moves the
// way the arrows and a gamepad move it.

using namespace jfd;
using jfd::library::ItemListModel;
using jfd::library::LiveTvGuide;
using jfd::library::LiveTvOnNow;
using jfd::library::LiveTvSchedule;
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

  QString stamp(const QDateTime& at) { return at.toUTC().toString(Qt::ISODate); }

  // Channel n: programmes from 15:00 to 17:00 the next day, 30 minutes long
  // (an hour on channel 2); channel 4 has nothing from 17:00 to 18:00.
  struct Program {
    QString id;
    int channel;
    QDateTime start;
    QDateTime end;
  };

  QList<Program> guide() {
    QList<Program> out;
    const QDateTime first = utc("2026-11-10T15:00:00Z");
    const QDateTime last = utc("2026-11-11T17:00:00Z");
    for (int n = 1; n <= 4; ++n) {
      const int minutes = n == 2 ? 60 : 30;
      for (QDateTime at = first; at < last; at = at.addSecs(minutes * 60)) {
        if (n == 4 && at >= utc("2026-11-10T17:00:00Z") && at < utc("2026-11-10T18:00:00Z")) continue;
        out.append({QStringLiteral("c%1-%2").arg(n).arg(at.toString(QStringLiteral("ddHHmm"))), n, at,
                    at.addSecs(minutes * 60)});
      }
    }
    std::stable_sort(out.begin(), out.end(), [](const Program& a, const Program& b) { return a.start < b.start; });
    return out;
  }

  QByteArray channelJson(int n, const QList<Program>& programs, const QDateTime& now) {
    QByteArray json = "{\"Id\": \"c" + QByteArray::number(n) + "\", \"Name\": \"Test Channel " +
                      QByteArray::number(n) + "\", \"ChannelNumber\": \"" + QByteArray::number(n) + "\"";
    if (n == 1) json += ", \"ImageTags\": {\"Primary\": \"logo\"}";
    for (const Program& p : programs) {
      if (p.channel == n && p.start <= now && now < p.end)
        json += ", \"CurrentProgram\": {\"Id\": \"" + p.id.toUtf8() + "\", \"Name\": \"Show " + p.id.toUtf8() +
                "\", \"StartDate\": \"" + stamp(p.start).toUtf8() + "\", \"EndDate\": \"" + stamp(p.end).toUtf8() +
                "\"}";
    }
    return json + "}";
  }

  // A server that filters the guide the way Jellyfin does (minEndDate,
  // maxStartDate, channelIds) and pages it.
  struct Stand {
    QList<Program> programs = guide();
    QDateTime* clock;

    HttpResponse operator()(const HttpRequest& r) const {
      const int start = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
      const int limit = std::max(1, r.query.queryItemValue(QStringLiteral("limit")).toInt());
      if (r.path == QStringLiteral("/LiveTv/Channels")) {
        QByteArray items;
        for (int n = start + 1; n <= std::min(4, start + limit); ++n)
          items += (items.isEmpty() ? "" : ",") + channelJson(n, programs, *clock);
        return ok(jfd::test::page(items, 4, start));
      }
      if (r.path.startsWith(QStringLiteral("/LiveTv/Channels/"))) {
        const int n = r.path.mid(QStringLiteral("/LiveTv/Channels/c").size()).toInt();
        return ok(channelJson(n, programs, *clock));
      }
      if (r.path == QStringLiteral("/LiveTv/Programs")) {
        const QDateTime minEnd = utc(r.query.queryItemValue(QStringLiteral("minEndDate")).toLatin1().constData());
        const QDateTime maxStart = utc(r.query.queryItemValue(QStringLiteral("maxStartDate")).toLatin1().constData());
        const QString channels = r.query.queryItemValue(QStringLiteral("channelIds"));
        QList<Program> matching;
        for (const Program& p : programs) {
          if (p.end <= minEnd || p.start >= maxStart) continue;
          if (!channels.isEmpty() && channels != QStringLiteral("c%1").arg(p.channel)) continue;
          matching.append(p);
        }
        QByteArray items;
        for (int i = start; i < std::min(int(matching.size()), start + limit); ++i) {
          const Program& p = matching[i];
          items += (items.isEmpty() ? "" : ",") + QByteArray("{\"Id\": \"") + p.id.toUtf8() + "\", \"ChannelId\": \"c" +
                   QByteArray::number(p.channel) + "\", \"Name\": \"Show " + p.id.toUtf8() + "\", \"StartDate\": \"" +
                   stamp(p.start).toUtf8() + "\", \"EndDate\": \"" + stamp(p.end).toUtf8() + "\"}";
        }
        return ok(jfd::test::page(items, int(matching.size()), start));
      }
      return HttpResponse{404, {}, {}, 0};
    }
  };

  struct GuideFixture {
    QDateTime clock = utc("2026-11-10T16:15:00Z");
    LibraryFixture f{Stand{guide(), &clock}};
    LiveTvGuide model{f.client, options(&clock)};

    static LiveTvGuide::Options options(QDateTime* clock) {
      LiveTvGuide::Options o;
      o.channelPageSize = 3;
      o.programPageSize = 50;
      o.now = [clock] { return *clock; };
      return o;
    }

    void load() {
      model.setViewMinutes(180);
      model.reload();
      REQUIRE(model.loading());
      REQUIRE(waitFor([&] { return !model.loading(); }));
      REQUIRE(model.errorText().isEmpty());
    }

    QVariantList cells(int row) const {
      return model.data(model.index(row), LiveTvGuide::ProgramsRole).toList();
    }
    QString focusedId() const { return model.focusedProgram().value(QStringLiteral("id")).toString(); }
  };
} // namespace

TEST_CASE("The guide starts at the half hour before now with every channel and the first chunk") {
  GuideFixture t;
  t.load();
  CHECK(t.model.count() == 4);
  CHECK(t.model.rangeStart() == utc("2026-11-10T16:00:00Z"));
  CHECK(t.model.rangeMinutes() == 24 * 60);
  CHECK(t.model.nowMinutes() == doctest::Approx(15.0));
  // Channels over two pages; one chunk (6 h) of the guide for the view.
  CHECK(t.f.count(QStringLiteral("/LiveTv/Channels")) == 2);
  CHECK(t.model.chunksRequested() == 1);
  CHECK(t.model.data(t.model.index(0), LiveTvGuide::NameRole).toString() == QStringLiteral("Test Channel 1"));
  CHECK(t.model.data(t.model.index(0), LiveTvGuide::ImageUrlRole).toUrl().path() ==
        QStringLiteral("/Items/c1/Images/Primary"));

  // A row lists only what is in view and an hour after it (no hour before
  // the start of the axis but the programme already on): 16:00..20:00.
  const QVariantList row1 = t.cells(0);
  REQUIRE_FALSE(row1.isEmpty());
  CHECK(row1.size() == 8);
  const QVariantMap first = row1.first().toMap();
  CHECK(first.value(QStringLiteral("offset")).toDouble() == doctest::Approx(0.0));
  CHECK(first.value(QStringLiteral("length")).toDouble() == doctest::Approx(30.0));
  CHECK(t.cells(1).size() == 4);
  CHECK(t.cells(1).first().toMap().value(QStringLiteral("length")).toDouble() == doctest::Approx(60.0));

  // The focus is at now on the first channel.
  CHECK(t.model.focusRow() == 0);
  CHECK(t.model.focusMinutes() == 15);
  CHECK(t.focusedId() == QStringLiteral("c1-101600"));
  CHECK(t.model.focusedChannelId() == QStringLiteral("c1"));
}

TEST_CASE("Arrows move along a row by programme and across rows at the same time") {
  GuideFixture t;
  t.load();
  // Right: the programme after, from its start.
  CHECK(t.model.moveFocus(1, 0));
  CHECK(t.focusedId() == QStringLiteral("c1-101630"));
  CHECK(t.model.focusMinutes() == 30);
  // Down to the hour-long one on channel 2, which covers 16:30.
  CHECK(t.model.moveFocus(0, 1));
  CHECK(t.model.focusRow() == 1);
  CHECK(t.focusedId() == QStringLiteral("c2-101600"));
  // Right on channel 2 jumps a whole hour.
  CHECK(t.model.moveFocus(1, 0));
  CHECK(t.focusedId() == QStringLiteral("c2-101700"));
  CHECK(t.model.focusMinutes() == 60);
  // Down twice: channel 4 has nothing at 17:00; the row still has focus.
  CHECK(t.model.moveFocus(0, 1));
  CHECK(t.model.moveFocus(0, 1));
  CHECK(t.model.focusRow() == 3);
  CHECK(t.model.focusedProgram().isEmpty());
  CHECK(t.model.focusedChannelId() == QStringLiteral("c4"));
  // Right through the empty hour by slots, then onto 18:00.
  CHECK(t.model.moveFocus(1, 0));
  CHECK(t.model.focusMinutes() == 90);
  CHECK(t.model.focusedProgram().isEmpty());
  CHECK(t.model.moveFocus(1, 0));
  CHECK(t.focusedId() == QStringLiteral("c4-101800"));
  // Left goes back into the gap, then to the programme before it.
  CHECK(t.model.moveFocus(-1, 0));
  CHECK(t.model.focusedProgram().isEmpty());
  CHECK(t.model.moveFocus(-1, 0));
  CHECK(t.model.moveFocus(-1, 0));
  CHECK(t.focusedId() == QStringLiteral("c4-101630"));
  // Edges: no row below the last, nothing before the start of the axis.
  CHECK_FALSE(t.model.moveFocus(0, 1));
  CHECK(t.model.moveFocus(-1, 0));
  CHECK(t.focusedId() == QStringLiteral("c4-101600"));
  CHECK_FALSE(t.model.moveFocus(-1, 0));
  CHECK(t.model.focusMinutes() == 0);
}

TEST_CASE("The view follows the focus and loads the guide's chunks as it gets there") {
  GuideFixture t;
  t.load();
  QSignalSpy cellsChanged(&t.model, &QAbstractItemModel::dataChanged);
  // Paging moves a screen (three hours) at a time; the view goes along.
  CHECK(t.model.pageFocus(1));
  CHECK(t.model.focusMinutes() == 195);
  CHECK(t.model.viewStartMinutes() == 180);
  CHECK(cellsChanged.count() > 0);
  // The view now reaches past six hours: the second chunk loads, once.
  REQUIRE(waitFor([&] { return t.model.chunksRequested() == 2 && !t.model.loading(); }));
  CHECK(t.cells(0).first().toMap().value(QStringLiteral("offset")).toDouble() == doctest::Approx(120.0));
  CHECK(t.model.pageFocus(1));
  CHECK(t.model.pageFocus(1));
  CHECK(t.model.viewStartMinutes() == 540);
  REQUIRE(waitFor([&] { return t.model.chunksRequested() == 3; }));
  waitFor([] { return false; }, 100);
  CHECK(t.model.chunksRequested() == 3);
  // Back to now.
  t.model.focusNow();
  CHECK(t.model.focusMinutes() == 15);
  CHECK(t.model.viewStartMinutes() == 0);
  CHECK(t.focusedId() == QStringLiteral("c1-101600"));
  // A programme across a chunk's edge is listed once.
  const QList<api::LiveTvProgram> programs = t.model.programsAt(0);
  for (qsizetype i = 1; i < programs.size(); ++i) CHECK(programs[i].id != programs[i - 1].id);
  // Scrolling by hand moves the view only, inside the axis.
  t.model.setViewStartMinutes(100000);
  CHECK(t.model.viewStartMinutes() == 24 * 60 - 180);
  CHECK(t.model.focusMinutes() == 15);
}

TEST_CASE("Now moves on with the clock; a reload keeps the focused channel") {
  GuideFixture t;
  t.load();
  QSignalSpy now(&t.model, &LiveTvGuide::nowChanged);
  t.clock = utc("2026-11-10T16:45:00Z");
  t.model.tick();
  CHECK(now.count() == 1);
  CHECK(t.model.nowMinutes() == doctest::Approx(45.0));
  t.model.moveFocus(0, 1);
  t.model.moveFocus(0, 1);
  t.model.reload();
  REQUIRE(waitFor([&] { return !t.model.loading(); }));
  CHECK(t.model.rangeStart() == utc("2026-11-10T16:30:00Z"));
  CHECK(t.model.focusRow() == 2);
  CHECK(t.model.focusMinutes() == 15);
  t.model.clear();
  CHECK(t.model.count() == 0);
  CHECK_FALSE(t.model.moveFocus(1, 0));
}

TEST_CASE("A guide that fails says so; one without channels is empty") {
  SUBCASE("fails") {
    LibraryFixture f([](const HttpRequest&) { return HttpResponse{500, {}, {}, 0}; });
    LiveTvGuide model(f.client);
    model.reload();
    REQUIRE(waitFor([&] { return !model.loading(); }));
    CHECK_FALSE(model.errorText().isEmpty());
    CHECK(model.count() == 0);
  }
  SUBCASE("empty") {
    LibraryFixture f([](const HttpRequest& r) {
      if (r.path == QStringLiteral("/LiveTv/Channels")) return ok(jfd::test::page({}, 0));
      return HttpResponse{500, {}, {}, 0};
    });
    LiveTvGuide model(f.client);
    model.reload();
    REQUIRE(waitFor([&] { return !model.loading(); }));
    CHECK(model.errorText().isEmpty());
    CHECK(model.count() == 0);
    CHECK(f.count(QStringLiteral("/LiveTv/Programs")) == 0);
  }
}

TEST_CASE("On now: a card per channel with a programme on, its progress moving on") {
  QDateTime clock = utc("2026-11-10T16:15:00Z");
  QList<Program> programs = guide();
  // Channel 3 has nothing on now.
  programs.erase(std::remove_if(programs.begin(), programs.end(),
                                [](const Program& p) { return p.channel == 3; }),
                 programs.end());
  LibraryFixture f(Stand{programs, &clock});
  LiveTvOnNow::Options o;
  o.now = [&clock] { return clock; };
  LiveTvOnNow model(f.client, o);
  model.reload();
  REQUIRE(waitFor([&] { return model.count() == 3; }));
  const auto at = [&](int row, int role) { return model.data(model.index(row), role); };
  CHECK(at(0, ItemListModel::IdRole).toString() == QStringLiteral("c1"));
  CHECK(at(0, ItemListModel::TypeRole).toString() == QStringLiteral("TvChannel"));
  CHECK(at(0, ItemListModel::NameRole).toString() == QStringLiteral("Show c1-101600"));
  CHECK(at(0, ItemListModel::SubtitleRole).toString() == QStringLiteral("1 · Test Channel 1"));
  CHECK(at(0, ItemListModel::ProgressRole).toDouble() == doctest::Approx(0.5));
  CHECK(at(0, ItemListModel::ImageUrlRole).toUrl().path() == QStringLiteral("/Items/c1/Images/Primary"));
  CHECK(at(1, ItemListModel::ProgressRole).toDouble() == doctest::Approx(0.25));
  CHECK(at(2, ItemListModel::IdRole).toString() == QStringLiteral("c4"));
  CHECK(model.roleNames().value(ItemListModel::IdRole) == QByteArray("itemId"));
  const QString request = f.server.requests().last().query.queryItemValue(QStringLiteral("addCurrentProgram"));
  CHECK(request == QStringLiteral("true"));

  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  clock = utc("2026-11-10T16:20:00Z");
  model.tick();
  CHECK(changed.count() == 1);
  // A programme ended: the row loads again.
  clock = utc("2026-11-10T16:35:00Z");
  model.tick();
  REQUIRE(waitFor([&] { return model.data(model.index(0), ItemListModel::NameRole).toString() ==
                               QStringLiteral("Show c1-101630"); }));
  CHECK(f.count(QStringLiteral("/LiveTv/Channels")) == 2);
  model.clear();
  CHECK(model.count() == 0);
}

TEST_CASE("A channel's page: the channel, then its programmes from now; ended ones drop off") {
  QDateTime clock = utc("2026-11-10T16:15:00Z");
  LibraryFixture f(Stand{guide(), &clock});
  LiveTvSchedule::Options o;
  o.hours = 3;
  o.programPageSize = 4;
  o.now = [&clock] { return clock; };
  LiveTvSchedule model(f.client, o);
  model.setChannelId(QStringLiteral("c2"));
  CHECK(model.loading());
  REQUIRE(waitFor([&] { return !model.loading(); }));
  CHECK(model.errorText().isEmpty());
  CHECK(model.name() == QStringLiteral("Test Channel 2"));
  CHECK(model.number() == QStringLiteral("2"));
  // 16:00, 17:00, 18:00, 19:00 (starts before 19:15).
  REQUIRE(model.count() == 4);
  const auto at = [&](int row, int role) { return model.data(model.index(row), role); };
  CHECK(at(0, LiveTvSchedule::TitleRole).toString() == QStringLiteral("Show c2-101600"));
  CHECK(at(0, LiveTvSchedule::OnNowRole).toBool());
  CHECK(at(0, LiveTvSchedule::ProgressRole).toDouble() == doctest::Approx(0.25));
  CHECK_FALSE(at(1, LiveTvSchedule::OnNowRole).toBool());
  CHECK(at(1, LiveTvSchedule::StartRole).toDateTime() == utc("2026-11-10T17:00:00Z"));
  bool askedForChannel = false;
  for (const HttpRequest& r : f.server.requests())
    if (r.path == QStringLiteral("/LiveTv/Programs"))
      askedForChannel = r.query.queryItemValue(QStringLiteral("channelIds")) == QStringLiteral("c2");
  CHECK(askedForChannel);

  clock = utc("2026-11-10T17:05:00Z");
  model.tick();
  CHECK(model.count() == 3);
  CHECK(at(0, LiveTvSchedule::OnNowRole).toBool());
}
