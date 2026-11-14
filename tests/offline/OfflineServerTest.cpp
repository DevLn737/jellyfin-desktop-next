#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/Downloads.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/ServerTime.h>
#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/DownloadWorker.h>
#include <jfd/offline/ExtrasDownloader.h>
#include <jfd/offline/JournalReportSink.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/OfflineLocalSource.h>
#include <jfd/offline/OfflineSync.h>
#include <jfd/offline/ViewJournal.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <memory>
#include <optional>

// Offline downloads against the live server started by scripts/test-server
// (JFD_TEST_SERVER_URL, JFD_TEST_USER, JFD_TEST_PASSWORD), Jellyfin 12.2:
// what Items/{id}/Download does with Range and If-Range, a download resumed
// from a .part, the extras of a finished item, and the watch journal's
// UserData writes (the date kept as sent, PlayCount set, not added to).
// The items' watched state is put back at the end.

using namespace jfd;
using namespace jfd::api;
using namespace jfd::offline;
using jfd::test::waitFor;

namespace {
  constexpr std::int64_t TicksPerSecond = 10'000'000;

  template <class T> Result<T> run(std::function<void(Callback<T>)> call, int timeoutMs = 20000) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    if (!waitFor([&] { return out.has_value(); }, timeoutMs)) {
      ApiError error;
      error.kind = ApiError::Kind::Timeout;
      error.message = QStringLiteral("test timed out");
      return error;
    }
    return std::move(*out);
  }

  struct Stand {
    std::unique_ptr<JellyfinClient> client;
    QString serverId;

    Stand() {
      client = std::make_unique<JellyfinClient>(ClientIdentity{
          QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"), QStringLiteral("CI offline"),
          QStringLiteral("ci-offline")});
      const auto url = JellyfinClient::normalizeServerUrl(qEnvironmentVariable("JFD_TEST_SERVER_URL"));
      REQUIRE_MESSAGE(url.has_value(), "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
      client->setServerUrl(*url);
      auto auth = run<AuthenticationResult>([&](auto done) {
        client->signIn(qEnvironmentVariable("JFD_TEST_USER"), qEnvironmentVariable("JFD_TEST_PASSWORD"),
                       nullptr, done);
      });
      REQUIRE_MESSAGE(auth.ok(), (auth.ok() ? QString() : auth.error().message).toStdString());
      serverId = auth->serverId;
      if (serverId.isEmpty()) serverId = QStringLiteral("0123456789abcdef0123456789abcdef");
    }

    // Items of a type with their media sources (server JSON).
    QList<QJsonObject> items(const QString& type) {
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("userId"), client->userId());
      query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
      query.addQueryItem(QStringLiteral("IncludeItemTypes"), type);
      query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("MediaSources,Path"));
      query.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
      auto page = run<QJsonObject>([&](auto done) {
        client->sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt, nullptr, done);
      });
      REQUIRE(page.ok());
      QList<QJsonObject> out;
      for (const auto& value : page->value(QLatin1String("Items")).toArray())
        out.append(value.toObject());
      REQUIRE_FALSE(out.isEmpty());
      return out;
    }

    QJsonObject userData(const QString& itemId) {
      auto data = run<QJsonObject>([&](auto done) { fetchUserItemData(*client, itemId, nullptr, done); });
      REQUIRE(data.ok());
      return *data;
    }

    void setUserData(const QString& itemId, const QJsonObject& body) {
      auto data = run<QJsonObject>([&](auto done) { updateUserItemData(*client, itemId, body, nullptr, done); });
      REQUIRE(data.ok());
    }

    // As before the test: not played, no position, no date.
    void reset(const QString& itemId) {
      auto removed = run<JellyfinClient::RawResponse>([&](auto done) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("userId"), client->userId());
        client->send(JellyfinClient::Method::Delete, QStringLiteral("UserPlayedItems/") + itemId, query,
                     std::nullopt, nullptr, done);
      });
      CHECK(removed.ok());
      setUserData(itemId, {{QStringLiteral("Played"), false},
                           {QStringLiteral("PlayCount"), 0},
                           {QStringLiteral("PlaybackPositionTicks"), 0}});
    }
  };

  struct RawAnswer {
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
  };

  RawAnswer get(const QUrl& url, const QByteArray& authorization, const QList<QPair<QByteArray, QByteArray>>& extra) {
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", authorization);
    request.setRawHeader("Accept-Encoding", "identity");
    for (const auto& [name, value] : extra)
      request.setRawHeader(name, value);
    QNetworkReply* reply = network.get(request);
    REQUIRE(waitFor([&] { return reply->isFinished(); }, 30000));
    RawAnswer answer;
    answer.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    for (const auto& [name, value] : reply->rawHeaderPairs())
      answer.headers.insert(name.toLower(), value);
    answer.body = reply->readAll();
    reply->deleteLater();
    return answer;
  }

  QByteArray sha(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  }

  QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }
} // namespace

TEST_CASE("Items/{id}/Download on 12.2: 206 for Range, Last-Modified, If-Range, 416 past the end") {
  Stand stand;
  const QJsonObject movie = stand.items(QStringLiteral("Movie")).front();
  const QJsonObject source = movie.value(QLatin1String("MediaSources")).toArray().first().toObject();
  const auto size = static_cast<qint64>(source.value(QLatin1String("Size")).toDouble());
  REQUIRE(size > 2 * 1024 * 1024);
  const QUrl url = originalDownloadUrl(stand.client->serverUrl(), source.value(QLatin1String("Id")).toString());
  const QByteArray auth = stand.client->authorizationHeader();

  const RawAnswer full = get(url, auth, {});
  CHECK(full.status == 200);
  CHECK(full.body.size() == size);
  CHECK(full.headers.value("accept-ranges") == "bytes");
  const QByteArray lastModified = full.headers.value("last-modified");
  CHECK_FALSE(lastModified.isEmpty());
  MESSAGE("ETag: " << (full.headers.contains("etag") ? full.headers.value("etag").toStdString() : "none")
                   << ", Last-Modified: " << lastModified.toStdString());

  const RawAnswer tail = get(url, auth, {{"Range", "bytes=1048576-"}});
  CHECK(tail.status == 206);
  CHECK(tail.headers.value("content-range") ==
        "bytes 1048576-" + QByteArray::number(size - 1) + "/" + QByteArray::number(size));
  CHECK(tail.body == full.body.mid(1048576));

  const RawAnswer same = get(url, auth, {{"Range", "bytes=1048576-"}, {"If-Range", lastModified}});
  CHECK(same.status == 206);
  // The file "changed" since the .part was written: the whole file again.
  const RawAnswer stale =
      get(url, auth, {{"Range", "bytes=1048576-"}, {"If-Range", "Sat, 01 Jan 2000 00:00:00 GMT"}});
  CHECK(stale.status == 200);
  CHECK(stale.body.size() == size);

  const RawAnswer past = get(url, auth, {{"Range", "bytes=" + QByteArray::number(size) + "-"}});
  CHECK(past.status == 416);

  // Without the session: refused; the client never puts the token in the URL.
  CHECK(get(url, QByteArray(), {}).status == 401);
}

TEST_CASE("A .part resumes against 12.2 and the file matches; the extras follow") {
  Stand stand;
  const QJsonObject movie = stand.items(QStringLiteral("Movie")).front();
  const QJsonObject source = movie.value(QLatin1String("MediaSources")).toArray().first().toObject();
  const QString sourceId = source.value(QLatin1String("Id")).toString();
  const auto size = static_cast<qint64>(source.value(QLatin1String("Size")).toDouble());
  const QUrl url = originalDownloadUrl(stand.client->serverUrl(), sourceId);
  const QByteArray auth = stand.client->authorizationHeader();
  const QByteArray original = get(url, auth, {}).body;
  REQUIRE(original.size() == size);
  const QString lastModified = QString::fromLatin1(get(url, auth, {{"Range", "bytes=0-0"}}).headers.value("last-modified"));

  QTemporaryDir dir;
  // A first MiB from an earlier, interrupted attempt.
  const QString part = dir.filePath(QStringLiteral("media.part"));
  const QString final = dir.filePath(QStringLiteral("media.mkv"));
  {
    QFile file(part);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(original.left(1024 * 1024));
  }
  DownloadWorker worker(std::make_shared<StorageGuard>());
  qint64 offset = -1;
  int outcome = -1;
  QObject::connect(&worker, &DownloadWorker::transferStarted,
                   [&](const QString&, qint64 o, qint64, const QString&) { offset = o; });
  QObject::connect(&worker, &DownloadWorker::transferFinished,
                   [&](const QString&, int out, qint64) { outcome = out; });
  DownloadJob job;
  job.key = sourceId;
  job.url = url;
  job.serverUrl = stand.client->serverUrl();
  job.authorization = auth;
  job.partPath = part;
  job.finalPath = final;
  job.expectedSize = size;
  job.resumeValidator = lastModified;
  worker.start(job);
  REQUIRE(waitFor([&] { return outcome >= 0; }, 60000));
  CHECK(outcome == static_cast<int>(TransferOutcome::Complete));
  // 206: the MiB on disk was kept.
  CHECK(offset == 1024 * 1024);
  CHECK(sha(readAll(final)) == sha(original));

  // The whole way: queue, file, extras, local copy.
  DownloadStore store(dir.filePath(QStringLiteral("downloads")));
  OfflineCatalog catalog(store);
  DownloadQueue queue(store, catalog, std::make_shared<StorageGuard>());
  ExtrasDownloader extras(store, catalog);
  QObject::connect(&queue, &DownloadQueue::completed, &extras, [&](const QString& id) { extras.fetch(id); });
  bool extrasDone = false;
  QObject::connect(&extras, &ExtrasDownloader::finished, [&](const QString&, bool) { extrasDone = true; });
  const QString userId = stand.client->userId();
  queue.setAccess(stand.serverId, userId, stand.client->serverUrl(), auth);
  extras.setAccess(stand.serverId, userId, stand.client->serverUrl(), auth);
  REQUIRE(queue.enqueueOriginal(movie, source, movie.value(QLatin1String("UserData")).toObject()) ==
          EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return extrasDone; }, 60000));
  const auto manifest = catalog.find(stand.serverId, userId, movie.value(QLatin1String("Id")).toString());
  REQUIRE(manifest.has_value());
  CHECK(manifest->state == ItemState::Complete);
  CHECK(manifest->extrasComplete);
  CHECK(sha(readAll(store.mediaPath(*manifest))) == sha(original));
  // The stand's movie has an external SRT and a poster.
  CHECK_FALSE(manifest->subtitleFiles.isEmpty());
  CHECK(manifest->imageFiles.contains(QStringLiteral("Primary")));
  OfflineLocalSource local(store, catalog);
  local.setUser(stand.serverId, userId);
  local.setOnline(false);
  const auto copy = local.find(manifest->itemId);
  REQUIRE(copy.has_value());
  CHECK(copy->externalSubtitles.size() == manifest->subtitleFiles.size());
  for (const auto& track : copy->externalSubtitles)
    CHECK(readAll(track.url.toLocalFile()).contains("-->"));
  // No token anywhere in the download folder.
  QDirIterator it(store.root(), QDir::Files, QDirIterator::Subdirectories);
  const QByteArray token = stand.client->accessToken().toLatin1();
  while (it.hasNext())
    CHECK_FALSE(readAll(it.next()).contains(token));
}

TEST_CASE("The journal on 12.2: date kept as sent, PlayCount not doubled, the server's newer state wins") {
  Stand stand;
  const QList<QJsonObject> episodes = stand.items(QStringLiteral("Episode"));
  REQUIRE(episodes.size() >= 2);
  const QJsonObject movie = stand.items(QStringLiteral("Movie")).front();
  const QString film = movie.value(QLatin1String("Id")).toString();
  const QString episode = episodes.at(1).value(QLatin1String("Id")).toString();
  const QString other = episodes.at(0).value(QLatin1String("Id")).toString();
  const std::int64_t episodeTicks =
      static_cast<std::int64_t>(episodes.at(1).value(QLatin1String("RunTimeTicks")).toDouble());
  const std::int64_t filmTicks = static_cast<std::int64_t>(movie.value(QLatin1String("RunTimeTicks")).toDouble());
  REQUIRE(episodeTicks > 300 * TicksPerSecond);
  for (const QString& id : {film, episode, other})
    stand.reset(id);
  const int filmCount = stand.userData(film).value(QLatin1String("PlayCount")).toInt();

  QTemporaryDir dir;
  DownloadStore store(dir.filePath(QStringLiteral("downloads")));
  OfflineCatalog catalog(store);
  ViewJournal journal(dir.filePath(QStringLiteral("journal.jsonl")));
  // Recorded "yesterday", without the server.
  const std::int64_t yesterday = QDateTime::currentMSecsSinceEpoch() - 24 * 3600 * 1000;
  std::int64_t now = yesterday;
  JournalReportSink::Hooks hooks;
  hooks.online = [] { return false; };
  hooks.serverId = [&] { return stand.serverId; };
  hooks.userId = [&] { return stand.client->userId(); };
  hooks.clockOffsetMs = [] { return std::optional<std::int64_t>(0); };
  hooks.systemNowMs = [&] { return now; };
  JournalReportSink sink(*stand.client, journal, hooks);
  const auto play = [&](const QString& id, std::int64_t position) {
    PlaybackProgress p;
    p.itemId = id;
    p.playMethod = PlayMethod::DirectPlay;
    sink.started(p);
    p.positionTicks = position;
    sink.stopped(p, false, {});
  };
  play(episode, episodeTicks / 2);
  now += 60'000;
  play(film, filmTicks);
  now += 60'000;
  play(other, episodeTicks / 2);
  // Another device watched `other` later (now, server time).
  stand.setUserData(other, {{QStringLiteral("PlaybackPositionTicks"), static_cast<double>(30 * TicksPerSecond)},
                            {QStringLiteral("LastPlayedDate"), formatServerInstant(systemClockNow())}});
  REQUIRE_FALSE(stand.client->accessToken().isEmpty());
  CHECK_FALSE(readAll(journal.path()).contains(stand.client->accessToken().toLatin1()));

  OfflineSync sync(*stand.client, journal, store, catalog);
  sync.setUser(stand.serverId, stand.client->userId());
  bool finished = false;
  QObject::connect(&sync, &OfflineSync::finished, [&] { finished = true; });
  int sent = 0, won = 0;
  QObject::connect(&sync, &OfflineSync::journalSynced, [&](int s, int w) {
    sent = s;
    won = w;
  });
  sync.syncNow();
  REQUIRE(waitFor([&] { return finished; }, 30000));
  CHECK(sent == 2);
  CHECK(won == 1);
  CHECK(journal.isEmpty());

  const QJsonObject e = stand.userData(episode);
  CHECK(std::llabs(static_cast<std::int64_t>(e.value(QLatin1String("PlaybackPositionTicks")).toDouble()) -
                   episodeTicks / 2) <= 10 * TicksPerSecond);
  CHECK_FALSE(e.value(QLatin1String("Played")).toBool());
  // 12.2 keeps the date it is sent (not "now").
  const auto episodeDate = parseServerInstant(e.value(QLatin1String("LastPlayedDate")).toString());
  REQUIRE(episodeDate.has_value());
  CHECK(episodeDate->sinceOrigin().count() / 10'000 == yesterday);
  const QJsonObject f = stand.userData(film);
  CHECK(f.value(QLatin1String("Played")).toBool());
  CHECK(f.value(QLatin1String("PlayCount")).toInt() == filmCount + 1);
  // The server's newer state stood.
  CHECK(static_cast<std::int64_t>(stand.userData(other).value(QLatin1String("PlaybackPositionTicks")).toDouble()) ==
        30 * TicksPerSecond);

  // The same update sent again (as after a lost answer) changes nothing.
  ViewUpdate again{stand.serverId, stand.client->userId(), film,
                   QJsonObject{{QStringLiteral("Played"), true},
                               {QStringLiteral("PlayCount"), filmCount + 1},
                               {QStringLiteral("PlaybackPositionTicks"), 0},
                               {QStringLiteral("LastPlayedDate"), f.value(QLatin1String("LastPlayedDate"))}},
                   yesterday + 60'000};
  REQUIRE(journal.setUpdate(again, {}));
  finished = false;
  sync.syncNow();
  REQUIRE(waitFor([&] { return finished; }, 30000));
  CHECK(journal.isEmpty());
  CHECK(stand.userData(film).value(QLatin1String("PlayCount")).toInt() == filmCount + 1);

  for (const QString& id : {film, episode, other})
    stand.reset(id);
}
