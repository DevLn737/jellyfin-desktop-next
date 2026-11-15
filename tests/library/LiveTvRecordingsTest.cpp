#include "LibraryTestSupport.h"

#include <jfd/library/LiveTvRecorder.h>
#include <jfd/library/LiveTvRecordings.h>

#include <QSignalSpy>

// Recording on a fake server shaped like 12.2's DVR (answers taken from the
// extras stand): who may record, recording a programme and its series,
// cancelling, the schedule as a list, and the recordings with delete.

using namespace jfd;
using jfd::library::LiveTvRecorder;
using jfd::library::LiveTvRecordings;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::LibraryFixture;
using jfd::test::ok;
using jfd::test::waitFor;

namespace {
  constexpr const char* InfoJson =
      R"({"Services": [{"Name": "Emby", "Status": "Ok", "IsVisible": false, "Tuners": []}],
          "IsEnabled": true, "EnabledUsers": ["user1"]})";

  QByteArray userJson(bool admin, bool manage) {
    return QByteArray(R"({"Id": "user1", "Name": "alice", "Policy": {"IsAdministrator": )") +
           (admin ? "true" : "false") + R"(, "EnableLiveTvManagement": )" + (manage ? "true" : "false") +
           R"(, "EnableLiveTvAccess": true}})";
  }

  // The server's DVR: timers by programme, series timers that schedule
  // every programme of their series ("s-" ids), defaults per programme.
  struct Dvr {
    bool admin = false;
    bool manage = true;
    bool refuse = false;
    QList<QByteArray> timers{
        R"({"Id": "t1", "Type": "Timer", "Status": "InProgress", "ProgramId": "p1", "Name": "News",
            "ChannelName": "Test Channel 1", "StartDate": "2026-11-15T00:30:00Z", "EndDate": "2026-11-15T01:00:00Z"})",
        R"({"Id": "t0", "Type": "Timer", "Status": "Completed", "ProgramId": "p0", "Name": "Done",
            "StartDate": "2026-11-14T00:30:00Z", "EndDate": "2026-11-14T01:00:00Z"})",
        R"({"Id": "t3", "Type": "Timer", "Status": "New", "ProgramId": "p3", "Name": "Test Series",
            "SeriesTimerId": "s1", "StartDate": "2026-11-15T02:00:00Z", "EndDate": "2026-11-15T02:30:00Z"})",
        R"({"Id": "t2", "Type": "Timer", "Status": "New", "ProgramId": "p2", "Name": "Film",
            "StartDate": "2026-11-15T01:00:00Z", "EndDate": "2026-11-15T03:00:00Z"})"};
    QList<QByteArray> series{
        R"({"Id": "s1", "Type": "SeriesTimer", "Name": "Test Series", "ChannelName": "Test Channel 50"})",
        R"({"Id": "s0", "Type": "SeriesTimer", "Name": "Another Series", "ChannelName": "Test Channel 9"})"};
    QStringList posted;

    HttpResponse operator()(const HttpRequest& r) {
      if (r.path == QStringLiteral("/LiveTv/Info")) return ok(InfoJson);
      if (r.path == QStringLiteral("/Users/Me")) return ok(userJson(admin, manage));
      if (r.path == QStringLiteral("/LiveTv/Timers") && r.method == "GET") {
        QByteArray items;
        for (const QByteArray& t : timers) items += (items.isEmpty() ? "" : ",") + t;
        return ok(jfd::test::page(items, int(timers.size())));
      }
      if (r.path == QStringLiteral("/LiveTv/SeriesTimers") && r.method == "GET") {
        QByteArray items;
        for (const QByteArray& t : series) items += (items.isEmpty() ? "" : ",") + t;
        return ok(jfd::test::page(items, int(series.size())));
      }
      if (refuse) return HttpResponse{403, {}, {}, 0};
      if (r.path == QStringLiteral("/LiveTv/Timers/Defaults")) {
        const QString id = r.query.queryItemValue(QStringLiteral("programId"));
        return ok(R"({"Type": "SeriesTimer", "Id": "x", "ProgramId": ")" + id.toUtf8() +
                  R"(", "Name": "Test Series", "RecordNewOnly": true})");
      }
      if (r.path == QStringLiteral("/LiveTv/Timers") && r.method == "POST") {
        const QString id = QJsonDocument::fromJson(r.body).object().value(QStringLiteral("ProgramId")).toString();
        posted.append(QStringLiteral("timer:") + id);
        timers.append(R"({"Id": "t-)" + id.toUtf8() + R"(", "Type": "Timer", "Status": "New", "ProgramId": ")" +
                      id.toUtf8() + R"(", "Name": "Test Series", "StartDate": "2026-11-15T05:00:00Z"})");
        return HttpResponse{204, {}, {}, 0};
      }
      if (r.path == QStringLiteral("/LiveTv/SeriesTimers") && r.method == "POST") {
        const QString id = QJsonDocument::fromJson(r.body).object().value(QStringLiteral("ProgramId")).toString();
        posted.append(QStringLiteral("series:") + id);
        series.append(R"({"Id": "s-new", "Type": "SeriesTimer", "Name": "Test Series"})");
        // The server schedules the series' programmes.
        for (const char* p : {"p7", "p8"})
          timers.append(R"({"Id": "t-)" + QByteArray(p) + R"(", "Type": "Timer", "Status": "New", "ProgramId": ")" +
                        QByteArray(p) + R"(", "SeriesTimerId": "s-new", "Name": "Test Series"})");
        return HttpResponse{204, {}, {}, 0};
      }
      if (r.method == "DELETE" && r.path.startsWith(QStringLiteral("/LiveTv/Timers/"))) {
        const QByteArray id = r.path.mid(QStringLiteral("/LiveTv/Timers/").size()).toUtf8();
        posted.append(QStringLiteral("cancel:") + QString::fromUtf8(id));
        timers.erase(std::remove_if(timers.begin(), timers.end(),
                                    [&](const QByteArray& t) { return t.contains("\"Id\": \"" + id + "\""); }),
                     timers.end());
        return HttpResponse{204, {}, {}, 0};
      }
      if (r.method == "DELETE" && r.path.startsWith(QStringLiteral("/LiveTv/SeriesTimers/"))) {
        const QByteArray id = r.path.mid(QStringLiteral("/LiveTv/SeriesTimers/").size()).toUtf8();
        posted.append(QStringLiteral("cancelSeries:") + QString::fromUtf8(id));
        series.erase(std::remove_if(series.begin(), series.end(),
                                    [&](const QByteArray& t) { return t.contains("\"Id\": \"" + id + "\""); }),
                     series.end());
        timers.erase(std::remove_if(timers.begin(), timers.end(),
                                    [&](const QByteArray& t) { return t.contains("\"SeriesTimerId\": \"" + id + "\""); }),
                     timers.end());
        return HttpResponse{204, {}, {}, 0};
      }
      return HttpResponse{404, {}, {}, 0};
    }
  };

  struct RecorderFixture {
    std::shared_ptr<Dvr> dvr = std::make_shared<Dvr>();
    LibraryFixture f{[d = dvr](const HttpRequest& r) { return (*d)(r); }};
    LiveTvRecorder recorder{f.client};

    void makeAvailable() {
      recorder.checkAvailable();
      REQUIRE(waitFor([&] { return recorder.available() && recorder.count() > 0 && !recorder.loading(); }));
    }
    QVariant at(int row, int role) const { return recorder.data(recorder.index(row), role); }
  };
} // namespace

TEST_CASE("Recording is there for a user who may manage Live TV, or an administrator") {
  RecorderFixture t;
  t.dvr->manage = true;
  t.recorder.checkAvailable();
  REQUIRE(waitFor([&] { return t.recorder.available(); }));
  // Then the schedule is read.
  REQUIRE(waitFor([&] { return t.recorder.count() > 0; }));

  RecorderFixture admin;
  admin.dvr->manage = false;
  admin.dvr->admin = true;
  admin.recorder.checkAvailable();
  REQUIRE(waitFor([&] { return admin.recorder.available(); }));

  RecorderFixture viewer;
  viewer.dvr->manage = false;
  viewer.recorder.checkAvailable();
  REQUIRE(waitFor([&] { return viewer.f.count(QStringLiteral("/Users/Me")) == 1; }));
  waitFor([] { return false; }, 100);
  CHECK_FALSE(viewer.recorder.available());
  CHECK(viewer.f.count(QStringLiteral("/LiveTv/Timers")) == 0);

  // No Live TV for this user: the policy is not even asked.
  LibraryFixture none([](const HttpRequest& r) {
    if (r.path == QStringLiteral("/LiveTv/Info"))
      return ok(R"({"Services": [{"Name": "Emby", "Status": "Ok"}], "IsEnabled": true, "EnabledUsers": ["someone"]})");
    return ok(userJson(true, true));
  });
  LiveTvRecorder nobody(none.client);
  nobody.checkAvailable();
  REQUIRE(waitFor([&] { return none.count(QStringLiteral("/LiveTv/Info")) == 1; }));
  waitFor([] { return false; }, 100);
  CHECK_FALSE(nobody.available());
  CHECK(none.count(QStringLiteral("/Users/Me")) == 0);
}

TEST_CASE("The schedule: series recordings by name, then what is to be recorded by start") {
  RecorderFixture t;
  t.makeAvailable();
  // Two series, three timers (the completed one is the recordings' list).
  REQUIRE(t.recorder.count() == 5);
  CHECK(t.at(0, LiveTvRecorder::TitleRole).toString() == QStringLiteral("Another Series"));
  CHECK(t.at(0, LiveTvRecorder::IsSeriesRole).toBool());
  CHECK(t.at(1, LiveTvRecorder::IdRole).toString() == QStringLiteral("s1"));
  CHECK(t.at(2, LiveTvRecorder::IdRole).toString() == QStringLiteral("t1"));
  CHECK(t.at(2, LiveTvRecorder::RecordingRole).toBool());
  CHECK_FALSE(t.at(2, LiveTvRecorder::IsSeriesRole).toBool());
  CHECK(t.at(2, LiveTvRecorder::ChannelNameRole).toString() == QStringLiteral("Test Channel 1"));
  CHECK(t.at(3, LiveTvRecorder::IdRole).toString() == QStringLiteral("t2"));
  CHECK(t.at(4, LiveTvRecorder::IdRole).toString() == QStringLiteral("t3"));
  CHECK(t.at(4, LiveTvRecorder::SeriesTimerIdRole).toString() == QStringLiteral("s1"));
  CHECK_FALSE(t.at(3, LiveTvRecorder::RecordingRole).toBool());
  CHECK(t.recorder.timers().size() == 3);
  CHECK(t.recorder.timerFor(QStringLiteral("p2")) == QStringLiteral("t2"));
  CHECK(t.recorder.timerFor(QStringLiteral("p0")).isEmpty());
  CHECK(t.recorder.roleNames().value(LiveTvRecorder::IdRole) == QByteArray("timerId"));
}

TEST_CASE("Record, record a series, cancel: the server is asked and the schedule read again") {
  RecorderFixture t;
  t.makeAvailable();
  QSignalSpy changed(&t.recorder, &LiveTvRecorder::timersChanged);
  QSignalSpy messages(&t.recorder, &LiveTvRecorder::message);

  t.recorder.record(QStringLiteral("p5"));
  CHECK(t.recorder.busy());
  REQUIRE(waitFor([&] { return changed.count() == 1; }));
  CHECK_FALSE(t.recorder.busy());
  CHECK(t.dvr->posted == QStringList{QStringLiteral("timer:p5")});
  CHECK(t.recorder.timerFor(QStringLiteral("p5")) == QStringLiteral("t-p5"));
  REQUIRE(messages.count() == 1);
  CHECK(messages[0][0].toString() == QStringLiteral("Recording scheduled."));
  CHECK_FALSE(messages[0][1].toBool());

  // A series: the server schedules its programmes, the list shows them.
  t.recorder.recordSeries(QStringLiteral("p7"));
  REQUIRE(waitFor([&] { return changed.count() == 2; }));
  CHECK(t.dvr->posted.last() == QStringLiteral("series:p7"));
  CHECK(t.recorder.timerFor(QStringLiteral("p8")) == QStringLiteral("t-p8"));
  CHECK(t.recorder.seriesTimers().size() == 3);

  t.recorder.cancel(QStringLiteral("t-p5"));
  REQUIRE(waitFor([&] { return changed.count() == 3; }));
  CHECK(t.dvr->posted.last() == QStringLiteral("cancel:t-p5"));
  CHECK(t.recorder.timerFor(QStringLiteral("p5")).isEmpty());

  t.recorder.cancelSeries(QStringLiteral("s-new"));
  REQUIRE(waitFor([&] { return changed.count() == 4; }));
  CHECK(t.dvr->posted.last() == QStringLiteral("cancelSeries:s-new"));
  CHECK(t.recorder.timerFor(QStringLiteral("p8")).isEmpty());
  CHECK(t.recorder.seriesTimers().size() == 2);
  CHECK(messages.last()[0].toString() == QStringLiteral("Series recording cancelled."));
}

TEST_CASE("A refused recording says so; signing out drops what was on its way") {
  RecorderFixture t;
  t.makeAvailable();
  t.dvr->refuse = true;
  QSignalSpy messages(&t.recorder, &LiveTvRecorder::message);
  t.recorder.record(QStringLiteral("p5"));
  REQUIRE(waitFor([&] { return messages.count() == 1; }));
  CHECK(messages[0][0].toString() == QStringLiteral("The server does not allow you to record."));
  CHECK(messages[0][1].toBool());
  CHECK(t.dvr->posted.isEmpty());

  t.dvr->refuse = false;
  QSignalSpy changed(&t.recorder, &LiveTvRecorder::timersChanged);
  waitFor([&] { return !t.recorder.loading(); });
  changed.clear();
  messages.clear();
  t.recorder.record(QStringLiteral("p6"));
  t.recorder.clear();
  CHECK_FALSE(t.recorder.available());
  CHECK(t.recorder.count() == 0);
  CHECK_FALSE(t.recorder.busy());
  CHECK(changed.count() == 1);
  waitFor([] { return false; }, 300);
  // The answer came back to a signed-out recorder: no toast, no list.
  CHECK(messages.isEmpty());
  CHECK(t.recorder.count() == 0);
}

namespace {
  // Recordings over two pages: one recording now, an episode, a film.
  HttpResponse recordingsServer(const HttpRequest& r, QStringList* deleted, bool refuseDelete) {
    if (r.method == "DELETE") {
      if (refuseDelete) return HttpResponse{403, {}, {}, 0};
      deleted->append(r.path);
      return HttpResponse{204, {}, {}, 0};
    }
    if (r.path != QStringLiteral("/LiveTv/Recordings")) return HttpResponse{404, {}, {}, 0};
    const int start = r.query.queryItemValue(QStringLiteral("startIndex")).toInt();
    const QList<QByteArray> all{
        R"({"Id": "r3", "Name": "Channel 2 at 00:30", "Type": "Movie", "RunTimeTicks": 241413330,
            "DateCreated": "2026-11-15T00:53:59Z", "UserData": {"Played": true}})",
        R"({"Id": "r2", "Name": "Episode 4", "SeriesName": "Test Series", "Type": "Episode",
            "RunTimeTicks": 165413330, "DateCreated": "2026-11-15T00:57:18Z", "ImageTags": {"Primary": "i2"},
            "UserData": {"PlaybackPositionTicks": 50000000}})",
        R"({"Id": "r1", "Name": "Channel 1 at 00:30", "Type": "Recording", "Status": "InProgress",
            "TimerId": "t1", "ChannelName": "Test Channel 1", "StartDate": "2026-11-15T00:30:00Z",
            "EndDate": "2026-11-15T01:00:00Z"})"};
    QByteArray items;
    for (int i = start; i < std::min(int(all.size()), start + 2); ++i) items += (items.isEmpty() ? "" : ",") + all[i];
    return ok(jfd::test::page(items, int(all.size()), start));
  }
} // namespace

TEST_CASE("Recordings: recording now first, then newest first; a finished one is deleted with its file") {
  QStringList deleted;
  bool refuse = false;
  LibraryFixture f([&](const HttpRequest& r) { return recordingsServer(r, &deleted, refuse); });
  LiveTvRecordings::Options options;
  options.pageSize = 2;
  LiveTvRecordings model(f.client, options);
  model.reload();
  CHECK(model.loading());
  REQUIRE(waitFor([&] { return !model.loading(); }));
  CHECK(model.errorText().isEmpty());
  CHECK(f.count(QStringLiteral("/LiveTv/Recordings")) == 2);
  REQUIRE(model.count() == 3);
  const auto at = [&](int row, int role) { return model.data(model.index(row), role); };
  CHECK(at(0, LiveTvRecordings::IdRole).toString() == QStringLiteral("r1"));
  CHECK(at(0, LiveTvRecordings::InProgressRole).toBool());
  CHECK(at(0, LiveTvRecordings::TimerIdRole).toString() == QStringLiteral("t1"));
  CHECK(at(0, LiveTvRecordings::ChannelNameRole).toString() == QStringLiteral("Test Channel 1"));
  // An episode: the series is the title, the episode under it.
  CHECK(at(1, LiveTvRecordings::IdRole).toString() == QStringLiteral("r2"));
  CHECK(at(1, LiveTvRecordings::TitleRole).toString() == QStringLiteral("Test Series"));
  CHECK(at(1, LiveTvRecordings::SubtitleRole).toString() == QStringLiteral("Episode 4"));
  CHECK(at(1, LiveTvRecordings::RunTimeTicksRole).toLongLong() == 165413330);
  CHECK(at(1, LiveTvRecordings::PositionTicksRole).toLongLong() == 50000000);
  CHECK(at(1, LiveTvRecordings::ImageUrlRole).toUrl().path() == QStringLiteral("/Items/r2/Images/Primary"));
  CHECK(at(2, LiveTvRecordings::TitleRole).toString() == QStringLiteral("Channel 2 at 00:30"));
  CHECK(at(2, LiveTvRecordings::SubtitleRole).toString().isEmpty());
  CHECK(at(2, LiveTvRecordings::PlayedRole).toBool());

  QSignalSpy removed(&model, &LiveTvRecordings::removed);
  model.remove(QStringLiteral("r2"));
  REQUIRE(waitFor([&] { return removed.count() == 1; }));
  CHECK(deleted == QStringList{QStringLiteral("/Items/r2")});
  REQUIRE(model.count() == 2);
  CHECK(at(1, LiveTvRecordings::IdRole).toString() == QStringLiteral("r3"));

  refuse = true;
  QSignalSpy failed(&model, &LiveTvRecordings::failed);
  model.remove(QStringLiteral("r3"));
  REQUIRE(waitFor([&] { return failed.count() == 1; }));
  CHECK(failed[0][0].toString() == QStringLiteral("The server does not allow you to delete recordings."));
  CHECK(model.count() == 2);
}
