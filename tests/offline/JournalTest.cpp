#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "OfflineFixtures.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/ServerTime.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/JournalReportSink.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/OfflineSync.h>
#include <jfd/offline/ViewJournal.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <functional>
#include <utility>

using namespace jfd;
using namespace jfd::offline;
using namespace jfd::test;

namespace {

  constexpr std::int64_t TicksPerSecond = 10'000'000;
  constexpr std::int64_t Minute = 60'000;
  // A fixed system clock: 2026-11-01T10:00:00Z.
  constexpr std::int64_t T0 = 1793527200000;

  api::ClientIdentity identity() {
    return api::ClientIdentity{QStringLiteral("Test Client"), QStringLiteral("1.0"),
                               QStringLiteral("Test Device"), QStringLiteral("device1")};
  }

  QString isoMs(std::int64_t ms) {
    return api::formatServerInstant(core::ServerInstant::fromSinceOrigin(core::WallTicks(ms * 10'000)));
  }

  std::int64_t msOf(const QJsonValue& value) {
    const auto instant = api::parseServerInstant(value.toString());
    return instant ? instant->sinceOrigin().count() / 10'000 : -1;
  }

  // Jellyfin's user data as 12.2 handles it (checked on the test stand):
  // POST UserItems/{id}/UserData stores the values it is given, the date as
  // sent and PlayCount as a value.
  struct FakeJellyfin {
    struct Entry {
      std::int64_t runTimeTicks = 7200 * TicksPerSecond;
      QJsonObject userData{{QStringLiteral("PlaybackPositionTicks"), 0},
                           {QStringLiteral("PlayCount"), 0},
                           {QStringLiteral("Played"), false}};
      QString eTag = QStringLiteral("c0ffee");
      QString sourceId;
    };
    QHash<QString, Entry> items;
    int posts = 0;
    QHash<QString, int> postsFor;
    // The next N writes are applied, but answered too late for the client.
    int loseAnswers = 0;
    int failWrites = 0;
    FakeServer server{[this](const HttpRequest& r) { return handle(r); }};
    api::JellyfinClient client{identity()};

    FakeJellyfin() {
      client.setServerUrl(server.url());
      client.setSession(QString::fromLatin1(Token), UserId);
      client.setTimeoutMs(300);
    }

    QJsonObject itemJson(const QString& id, const Entry& e) const {
      return {{QStringLiteral("Id"), id},
              {QStringLiteral("RunTimeTicks"), static_cast<double>(e.runTimeTicks)},
              {QStringLiteral("UserData"), e.userData},
              {QStringLiteral("MediaSources"),
               QJsonArray{QJsonObject{{QStringLiteral("Id"), e.sourceId.isEmpty() ? id : e.sourceId},
                                      {QStringLiteral("ETag"), e.eTag}}}}};
    }

    // Runs when an item lookup arrives, before it is answered.
    std::function<void()> onLookup;
    HttpResponse handle(const HttpRequest& r) {
      if (r.method == "GET" && r.path == QLatin1String("/Items")) {
        if (onLookup) std::exchange(onLookup, nullptr)();
        QJsonArray found;
        for (const QString& id : r.query.queryItemValue(QStringLiteral("ids")).split(QLatin1Char(',')))
          if (items.contains(id)) found.append(itemJson(id, items[id]));
        return {200, QJsonDocument(QJsonObject{{QStringLiteral("Items"), found}}).toJson()};
      }
      const QStringList parts = r.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
      if (parts.size() == 3 && parts[0] == QLatin1String("UserItems") && parts[2] == QLatin1String("UserData")) {
        if (!items.contains(parts[1])) return {404, "{}"};
        Entry& e = items[parts[1]];
        if (r.method == "POST") {
          if (failWrites > 0) {
            --failWrites;
            return {503, "{}"};
          }
          ++posts;
          ++postsFor[parts[1]];
          const QJsonObject body = QJsonDocument::fromJson(r.body).object();
          for (auto it = body.begin(); it != body.end(); ++it)
            e.userData.insert(it.key(), it.value());
          HttpResponse response{200, QJsonDocument(e.userData).toJson()};
          if (loseAnswers > 0) {
            --loseAnswers;
            response.delayMs = 1500;
          }
          return response;
        }
        return {200, QJsonDocument(e.userData).toJson()};
      }
      return {404, "{}"};
    }

    // The other devices of the user, through the server.
    void watchedElsewhere(const QString& id, std::int64_t serverMs, bool played) {
      Entry& e = items[id];
      e.userData.insert(QStringLiteral("LastPlayedDate"), isoMs(serverMs));
      e.userData.insert(QStringLiteral("Played"), played);
      e.userData.insert(QStringLiteral("PlaybackPositionTicks"), 0);
    }
  };

  struct Offline {
    QTemporaryDir dir;
    DownloadStore store{dir.filePath(QStringLiteral("downloads"))};
    OfflineCatalog catalog{store};
    ViewJournal journal{dir.filePath(QStringLiteral("journal.jsonl"))};
  };

  // Reports of one playback of `itemId` at system time `atMs`, recorded
  // offline: start, a progress, stop at `positionTicks`.
  void playOffline(JournalReportSink& sink, const QString& itemId, std::int64_t positionTicks) {
    api::PlaybackProgress p;
    p.itemId = itemId;
    p.mediaSourceId = itemId;
    p.playMethod = api::PlayMethod::DirectPlay;
    sink.started(p);
    p.positionTicks = positionTicks / 2;
    sink.progress(p);
    p.positionTicks = positionTicks;
    sink.stopped(p, false, {});
  }

  JournalReportSink::Hooks offlineHooks(std::int64_t& now, std::optional<std::int64_t> offset) {
    JournalReportSink::Hooks hooks;
    hooks.online = [] { return false; };
    hooks.serverId = [] { return ServerId; };
    hooks.userId = [] { return UserId; };
    hooks.clockOffsetMs = [offset] { return offset; };
    hooks.systemNowMs = [&now] { return now; };
    return hooks;
  }

  bool sync(OfflineSync& sync) {
    bool finished = false;
    auto connection = QObject::connect(&sync, &OfflineSync::finished, [&] { finished = true; });
    sync.syncNow();
    const bool ok = waitFor([&] { return finished; }, 10000);
    QObject::disconnect(connection);
    return ok;
  }

} // namespace

TEST_CASE("How a playback counts follows Jellyfin's default rules") {
  const std::int64_t twoHours = 7200 * TicksPerSecond;
  CHECK(classifyView(0, twoHours) == ViewOutcome::Nothing);
  CHECK(classifyView(twoHours * 4 / 100, twoHours) == ViewOutcome::Nothing);
  CHECK(classifyView(twoHours / 2, twoHours) == ViewOutcome::Resume);
  CHECK(classifyView(twoHours * 91 / 100, twoHours) == ViewOutcome::Played);
  CHECK(classifyView(twoHours, twoHours) == ViewOutcome::Played);
  // Under 300 s (a song): past 5 % it is played.
  CHECK(classifyView(60 * TicksPerSecond, 200 * TicksPerSecond) == ViewOutcome::Played);
  CHECK(classifyView(5 * TicksPerSecond, 200 * TicksPerSecond) == ViewOutcome::Nothing);
}

TEST_CASE("Without the server every report goes to the journal, in server time") {
  Offline o;
  FakeJellyfin fake;
  std::int64_t now = T0;
  // The system clock is 10 minutes behind the server's.
  JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 10 * Minute));
  playOffline(sink, itemIdFor(1), 600 * TicksPerSecond);
  CHECK(fake.server.requests().isEmpty());
  const QList<ViewEvent> events = o.journal.events();
  REQUIRE(events.size() == 3);
  CHECK(events.back().stopped);
  CHECK(events.back().positionTicks == 600 * TicksPerSecond);
  CHECK(events.back().atMs == T0 + 10 * Minute);
  CHECK(events.back().clockOffsetMs == 10 * Minute);
  CHECK(events.back().clockKnown);
  CHECK(events.front().playId == events.back().playId);

  // After a restart without the server: the journal remembers the offset.
  ViewJournal reloaded(o.journal.path());
  CHECK(reloaded.events().size() == 3);
  REQUIRE(reloaded.clockOffsetMs().has_value());
  CHECK(*reloaded.clockOffsetMs() == 10 * Minute);
  JournalReportSink later(fake.client, reloaded, offlineHooks(now, std::nullopt));
  playOffline(later, itemIdFor(2), 10 * TicksPerSecond);
  CHECK(reloaded.events().back().atMs == T0 + 10 * Minute);
  CHECK(reloaded.events().back().clockKnown);
  CHECK(reloaded.events().back().playId != events.back().playId);
}

TEST_CASE("Online, only failed reports are journaled, and a later success clears them") {
  Offline o;
  int failing = 1;
  QList<QString> paths;
  FakeServer server([&](const HttpRequest& r) {
    paths.append(r.path);
    if (failing > 0) {
      --failing;
      return HttpResponse{503, "{}"};
    }
    return HttpResponse{204, {}};
  });
  api::JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("t"), UserId);
  std::int64_t now = T0;
  auto hooks = offlineHooks(now, 0);
  hooks.online = [] { return true; };
  JournalReportSink sink(client, o.journal, hooks);

  api::PlaybackProgress p;
  p.itemId = itemIdFor(3);
  p.playMethod = api::PlayMethod::DirectPlay;
  sink.started(p);
  REQUIRE(waitFor([&] { return o.journal.events().size() == 1; }));
  CHECK(paths.back() == QStringLiteral("/Sessions/Playing"));
  p.positionTicks = 50 * TicksPerSecond;
  sink.progress(p);
  // The progress reached the server: the failed start is out of date.
  REQUIRE(waitFor([&] { return o.journal.isEmpty(); }));
  bool answered = false;
  sink.stopped(p, false, [&] { answered = true; });
  REQUIRE(waitFor([&] { return answered; }));
  CHECK(o.journal.isEmpty());
  CHECK(paths.size() == 3);
}

TEST_CASE("Sync: half a film resumes, a finished album is played, position within 10 s") {
  Offline o;
  FakeJellyfin fake;
  const QString film = itemIdFor(10);
  const QString track = itemIdFor(11);
  fake.items[film] = {};
  fake.items[track].runTimeTicks = 200 * TicksPerSecond;
  fake.items[track].userData.insert(QStringLiteral("PlayCount"), 4);
  std::int64_t now = T0;
  JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 0));
  playOffline(sink, film, 3600 * TicksPerSecond);
  now += Minute;
  playOffline(sink, track, 200 * TicksPerSecond);

  OfflineSync s(fake.client, o.journal, o.store, o.catalog);
  s.setUser(ServerId, UserId);
  int sent = -1;
  QObject::connect(&s, &OfflineSync::journalSynced, [&](int n, int) { sent = n; });
  REQUIRE(sync(s));
  CHECK(sent == 2);
  CHECK(o.journal.isEmpty());
  const QJsonObject filmData = fake.items[film].userData;
  CHECK(std::llabs(static_cast<std::int64_t>(filmData.value(QStringLiteral("PlaybackPositionTicks")).toDouble()) -
                   3600 * TicksPerSecond) <= 10 * TicksPerSecond);
  CHECK_FALSE(filmData.value(QStringLiteral("Played")).toBool());
  CHECK(msOf(filmData.value(QStringLiteral("LastPlayedDate"))) == T0);
  const QJsonObject trackData = fake.items[track].userData;
  CHECK(trackData.value(QStringLiteral("Played")).toBool());
  CHECK(trackData.value(QStringLiteral("PlayCount")).toInt() == 5);
  CHECK(trackData.value(QStringLiteral("PlaybackPositionTicks")).toDouble() == 0);

  // A second sync changes nothing and sends nothing.
  const int posts = fake.posts;
  REQUIRE(sync(s));
  CHECK(fake.posts == posts);
  CHECK(fake.items[track].userData.value(QStringLiteral("PlayCount")).toInt() == 5);
}

TEST_CASE("A lost answer does not count a play twice") {
  Offline o;
  FakeJellyfin fake;
  const QString track = itemIdFor(12);
  fake.items[track].runTimeTicks = 180 * TicksPerSecond;
  fake.items[track].userData.insert(QStringLiteral("PlayCount"), 2);
  std::int64_t now = T0;
  JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 0));
  playOffline(sink, track, 180 * TicksPerSecond);

  OfflineSync s(fake.client, o.journal, o.store, o.catalog);
  s.setUser(ServerId, UserId);
  fake.loseAnswers = 1;
  REQUIRE(sync(s));
  // The server applied it, the client did not hear back: the target stays.
  CHECK(fake.items[track].userData.value(QStringLiteral("PlayCount")).toInt() == 3);
  REQUIRE(o.journal.update(ServerId, UserId, track).has_value());
  CHECK(o.journal.update(ServerId, UserId, track)->body.value(QStringLiteral("PlayCount")).toInt() == 3);
  CHECK(o.journal.events().isEmpty());

  // A restart in between: the target is read back from the file.
  ViewJournal reloaded(o.journal.path());
  OfflineSync again(fake.client, reloaded, o.store, o.catalog);
  again.setUser(ServerId, UserId);
  REQUIRE(sync(again));
  CHECK(fake.postsFor[track] == 2);
  CHECK(fake.items[track].userData.value(QStringLiteral("PlayCount")).toInt() == 3);
  CHECK(reloaded.isEmpty());
}

TEST_CASE("A failed write stays in the journal and is sent at the next sync") {
  Offline o;
  FakeJellyfin fake;
  const QString film = itemIdFor(13);
  fake.items[film] = {};
  std::int64_t now = T0;
  JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 0));
  playOffline(sink, film, 7000 * TicksPerSecond);
  OfflineSync s(fake.client, o.journal, o.store, o.catalog);
  s.setUser(ServerId, UserId);
  fake.failWrites = 1;
  REQUIRE(sync(s));
  CHECK_FALSE(o.journal.isEmpty());
  CHECK_FALSE(fake.items[film].userData.value(QStringLiteral("Played")).toBool());
  REQUIRE(sync(s));
  CHECK(o.journal.isEmpty());
  CHECK(fake.items[film].userData.value(QStringLiteral("Played")).toBool());
  CHECK(fake.items[film].userData.value(QStringLiteral("PlayCount")).toInt() == 1);
}

TEST_CASE("Who is newer is decided in server time: clocks 10 minutes apart either way") {
  const QString film = itemIdFor(14);
  SUBCASE("client 10 min behind: watched here after the other device") {
    Offline o;
    FakeJellyfin fake;
    // Real order: the other device at server T0+5 min, this one at server
    // T0+10 min (its own clock says T0).
    fake.watchedElsewhere(film, T0 + 5 * Minute, false);
    std::int64_t now = T0;
    JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 10 * Minute));
    playOffline(sink, film, 3000 * TicksPerSecond);
    OfflineSync s(fake.client, o.journal, o.store, o.catalog);
    s.setUser(ServerId, UserId);
    REQUIRE(sync(s));
    CHECK(fake.posts == 1);
    CHECK(msOf(fake.items[film].userData.value(QStringLiteral("LastPlayedDate"))) == T0 + 10 * Minute);
    CHECK(fake.items[film].userData.value(QStringLiteral("PlaybackPositionTicks")).toDouble() ==
          3000.0 * TicksPerSecond);
  }
  SUBCASE("client 10 min ahead: the other device watched later, the server wins") {
    Offline o;
    FakeJellyfin fake;
    // This one at server T0-10 min (its own clock says T0), the other
    // device at server T0-5 min.
    fake.watchedElsewhere(film, T0 - 5 * Minute, true);
    std::int64_t now = T0;
    JournalReportSink sink(fake.client, o.journal, offlineHooks(now, -10 * Minute));
    playOffline(sink, film, 3000 * TicksPerSecond);
    OfflineSync s(fake.client, o.journal, o.store, o.catalog);
    s.setUser(ServerId, UserId);
    int won = 0;
    QObject::connect(&s, &OfflineSync::journalSynced, [&](int, int serverWon) { won = serverWon; });
    REQUIRE(sync(s));
    CHECK(fake.posts == 0);
    CHECK(won == 1);
    CHECK(o.journal.isEmpty());
    CHECK(fake.items[film].userData.value(QStringLiteral("Played")).toBool());
  }
}

TEST_CASE("100 journaled items are sent once each; a repeated sync sends nothing") {
  Offline o;
  FakeJellyfin fake;
  std::int64_t now = T0;
  JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 0));
  for (int i = 0; i < 100; ++i) {
    const QString id = itemIdFor(100 + i);
    fake.items[id].runTimeTicks = 240 * TicksPerSecond;
    playOffline(sink, id, 240 * TicksPerSecond);
    now += 1000;
  }
  OfflineSync s(fake.client, o.journal, o.store, o.catalog);
  s.setUser(ServerId, UserId);
  s.setBatchSize(30);
  REQUIRE(sync(s));
  CHECK(fake.posts == 100);
  CHECK(fake.postsFor.size() == 100);
  for (auto it = fake.postsFor.begin(); it != fake.postsFor.end(); ++it)
    CHECK(it.value() == 1);
  CHECK(o.journal.isEmpty());
  REQUIRE(sync(s));
  CHECK(fake.posts == 100);
}

TEST_CASE("Items gone from the server and playbacks under 5 % leave the journal without a write") {
  Offline o;
  FakeJellyfin fake;
  const QString kept = itemIdFor(20);
  fake.items[kept] = {};
  std::int64_t now = T0;
  JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 0));
  playOffline(sink, itemIdFor(21), 3000 * TicksPerSecond);
  playOffline(sink, kept, 60 * TicksPerSecond);
  OfflineSync s(fake.client, o.journal, o.store, o.catalog);
  s.setUser(ServerId, UserId);
  REQUIRE(sync(s));
  CHECK(fake.posts == 0);
  CHECK(o.journal.isEmpty());
}

TEST_CASE("An event journaled while the lookup runs is kept for the next run, not dropped") {
  Offline o;
  FakeJellyfin fake;
  const QString first = itemIdFor(22);
  const QString late = itemIdFor(23);
  fake.items[first] = {};
  fake.items[late] = {};
  std::int64_t now = T0;
  JournalReportSink sink(fake.client, o.journal, offlineHooks(now, 0));
  playOffline(sink, first, 3000 * TicksPerSecond);
  // While the server looks up `first`, a report for `late` fails and is
  // journaled (a flaky connection right after reconnecting).
  fake.onLookup = [&] {
    now += Minute;
    playOffline(sink, late, 3000 * TicksPerSecond);
  };

  OfflineSync s(fake.client, o.journal, o.store, o.catalog);
  s.setUser(ServerId, UserId);
  int runs = 0;
  QObject::connect(&s, &OfflineSync::finished, [&] { ++runs; });
  s.syncNow();
  // The first run sends `first` only and leaves `late`; a second run
  // follows by itself and sends it.
  REQUIRE(waitFor([&] { return runs >= 2 && o.journal.isEmpty(); }, 10000));
  CHECK(fake.postsFor[first] == 1);
  CHECK(fake.postsFor[late] == 1);
  CHECK(std::llabs(static_cast<std::int64_t>(
                       fake.items[late].userData.value(QStringLiteral("PlaybackPositionTicks")).toDouble()) -
                   3000 * TicksPerSecond) <= 10 * TicksPerSecond);
}

TEST_CASE("Watched marks from other devices reach the manifests; gone and changed items are marked") {
  Offline o;
  FakeJellyfin fake;
  const auto add = [&](const QString& id) {
    const QJsonObject source = mediaSourceJson(id, 100);
    ItemManifest m;
    m.serverId = ServerId;
    m.userId = UserId;
    m.itemId = id;
    m.state = ItemState::Complete;
    m.item = itemJson(id, source);
    m.mediaSource = source;
    m.mediaFile = QStringLiteral("media.mkv");
    m.userData = QJsonObject{{QStringLiteral("Played"), false}};
    REQUIRE(o.store.writeManifest(m));
  };
  const QString watched = itemIdFor(40), gone = itemIdFor(41), changed = itemIdFor(42);
  add(watched);
  add(gone);
  add(changed);
  o.catalog.load();
  fake.items[watched] = {};
  fake.watchedElsewhere(watched, T0, true);
  fake.items[changed] = {};
  fake.items[changed].eTag = QStringLiteral("d1ffe7");

  OfflineSync s(fake.client, o.journal, o.store, o.catalog);
  s.setUser(ServerId, UserId);
  int refreshed = -1;
  QObject::connect(&s, &OfflineSync::manifestsRefreshed, [&](int n) { refreshed = n; });
  REQUIRE(sync(s));
  CHECK(refreshed == 3);
  const auto w = o.store.readManifest(ServerId, UserId, watched);
  REQUIRE(w.has_value());
  CHECK(w->userData.value(QStringLiteral("Played")).toBool());
  CHECK_FALSE(w->removedOnServer);
  CHECK_FALSE(w->changedOnServer);
  const auto g = o.store.readManifest(ServerId, UserId, gone);
  CHECK(g->removedOnServer);
  const auto c = o.store.readManifest(ServerId, UserId, changed);
  CHECK(c->changedOnServer);
  CHECK(o.catalog.find(ServerId, UserId, gone)->removedOnServer);

  // Nothing new: nothing written.
  REQUIRE(sync(s));
  CHECK(refreshed == 0);
  // Not a single secret in the folder or the journal.
  CHECK(filesContaining(o.dir.path(), {Token}).isEmpty());
}
