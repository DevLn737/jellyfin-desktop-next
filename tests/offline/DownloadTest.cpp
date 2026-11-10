#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "FakeFileServer.h"
#include "OfflineFixtures.h"

#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/DownloadWorker.h>
#include <jfd/offline/OfflineCatalog.h>

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

using namespace jfd::offline;
using namespace jfd::test;

namespace {

  QString downloadPath(const QString& id) {
    return QStringLiteral("/Items/") + id + QStringLiteral("/Download");
  }

  QByteArray fileSha(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return FakeFileServer::sha256(file.readAll());
  }

  // A queue on a temporary folder, signed in to `server`.
  struct Harness {
    QTemporaryDir dir;
    DownloadStore store{dir.filePath(QStringLiteral("downloads"))};
    OfflineCatalog catalog{store};
    std::shared_ptr<FakeStorageGuard> guard = std::make_shared<FakeStorageGuard>(0, 0);
    std::unique_ptr<DownloadQueue> queue;
    QStringList completed;
    QList<QPair<QString, QueueProblem>> failures;

    explicit Harness(const QUrl& serverUrl) { open(serverUrl); }

    void open(const QUrl& serverUrl) {
      queue.reset();
      catalog.load();
      queue = std::make_unique<DownloadQueue>(store, catalog, guard);
      queue->setRetryDelaysMs({10, 10, 10, 10, 10});
      // A cut answer stalls (FakeFileServer::cutAfter); give up on it soon.
      queue->setStallTimeoutMs(1000);
      QObject::connect(queue.get(), &DownloadQueue::completed,
                       [this](const QString& id) { completed.append(id); });
      QObject::connect(queue.get(), &DownloadQueue::failed,
                       [this](const QString& id, QueueProblem p) { failures.append({id, p}); });
      queue->setAccess(ServerId, UserId, serverUrl, "MediaBrowser Token=\"" + Token + "\"");
    }

    QString mediaPath(const QString& id) const {
      const auto m = catalog.find(ServerId, UserId, id);
      return m ? store.mediaPath(*m) : QString();
    }

    EnqueueResult add(const QString& id, qint64 size) {
      const QJsonObject source = mediaSourceJson(id, size);
      return queue->enqueueOriginal(itemJson(id, source), source,
                                    QJsonObject{{QStringLiteral("Played"), false}});
    }
  };

} // namespace

TEST_CASE("A download cut at 50 % resumes with Range and If-Range and matches byte for byte") {
  FakeFileServer server;
  const QString id = itemIdFor(1);
  const QByteArray content = patternBytes(3 * MiB + 17, 1);
  server.setFile(downloadPath(id), {content});
  server.cutAfter = content.size() / 2;
  server.cutTimes = 1;

  Harness h(server.url());
  REQUIRE(h.add(id, content.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty() || !h.failures.isEmpty(); }, 15000));
  REQUIRE(h.completed == QStringList{id});

  CHECK(fileSha(h.mediaPath(id)) == FakeFileServer::sha256(content));
  REQUIRE(server.requests().size() == 2);
  CHECK(server.requests()[0].range.isEmpty());
  const FileRequest& resumed = server.requests()[1];
  CHECK(resumed.range.startsWith("bytes="));
  CHECK(resumed.range.mid(6).split('-').value(0).toLongLong() > 0);
  CHECK(resumed.ifRange == QByteArray("\"v1\""));
  CHECK(resumed.status == 206);
  // Threshold "Downloads: resume": at most 1 MiB fetched twice.
  CHECK(server.bodyBytesSent() - content.size() <= MiB);
  // The token went in the header, to this server only.
  for (const auto& r : server.requests())
    CHECK(r.authorization.contains(Token));

  const auto m = h.catalog.find(ServerId, UserId, id);
  REQUIRE(m);
  CHECK(m->state == ItemState::Complete);
  CHECK(m->expectedSize == content.size());
  CHECK_FALSE(QFile::exists(h.store.partPath(*m)));
}

TEST_CASE("A connection closed mid-body resumes from what is on disk") {
  FakeFileServer server;
  const QString id = itemIdFor(14);
  const QByteArray content = patternBytes(2 * MiB + 3, 14);
  server.setFile(downloadPath(id), {content});
  server.cutAfter = MiB;
  server.cutTimes = 1;
  server.closeOnCut = true;

  Harness h(server.url());
  REQUIRE(h.add(id, content.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty() || !h.failures.isEmpty(); }, 15000));
  REQUIRE(h.completed == QStringList{id});
  CHECK(fileSha(h.mediaPath(id)) == FakeFileServer::sha256(content));
  // Qt reports the short body as an error or as a finished reply, depending
  // on timing; either way the download continued with a range.
  bool resumed = false;
  for (const auto& r : server.requests())
    resumed = resumed || (r.status == 206 && r.ifRange == QByteArray("\"v1\"") &&
                          r.range.mid(6).split('-').value(0).toLongLong() > 0);
  CHECK(resumed);
}

TEST_CASE("A server that ignores Range sends the whole file again, and it still matches") {
  FakeFileServer server;
  const QString id = itemIdFor(2);
  const QByteArray content = patternBytes(2 * MiB, 2);
  server.setFile(downloadPath(id), {content});
  server.cutAfter = MiB + 5;
  server.cutTimes = 1;
  server.ignoreRanges = true;

  Harness h(server.url());
  REQUIRE(h.add(id, content.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty() || !h.failures.isEmpty(); }, 15000));
  REQUIRE(h.completed == QStringList{id});
  CHECK(fileSha(h.mediaPath(id)) == FakeFileServer::sha256(content));
  REQUIRE(server.requests().size() == 2);
  CHECK(server.requests()[1].status == 200);
}

TEST_CASE("A file that changed on the server (new ETag) is downloaded from the start") {
  FakeFileServer server;
  const QString id = itemIdFor(3);
  const QByteArray before = patternBytes(2 * MiB, 3);
  const QByteArray after = patternBytes(2 * MiB, 4);
  server.setFile(downloadPath(id), {before});
  server.cutAfter = MiB;
  server.cutTimes = 1;
  // Replace the file once the first, cut answer is out.
  server.onRequest = [&](const FileRequest&) {
    if (server.requests().size() == 2)
      server.setFile(downloadPath(id), {after, "\"v2\"", "Tue, 03 Nov 2026 10:00:00 GMT"});
  };

  Harness h(server.url());
  REQUIRE(h.add(id, before.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty() || !h.failures.isEmpty(); }, 15000));
  REQUIRE(h.completed == QStringList{id});
  REQUIRE(server.requests().size() == 2);
  CHECK(server.requests()[1].ifRange == QByteArray("\"v1\""));
  CHECK(server.requests()[1].status == 200);
  CHECK(fileSha(h.mediaPath(id)) == FakeFileServer::sha256(after));
}

TEST_CASE("Five retries with growing delays, then the item fails; Retry starts it again") {
  FakeFileServer server;
  const QString id = itemIdFor(4);
  const QByteArray content = patternBytes(64 * 1024, 5);
  server.setFile(downloadPath(id), {content});
  server.failStatus = 503;
  server.failTimes = -1;

  Harness h(server.url());
  h.queue->setRetryDelaysMs({20, 40, 80, 160, 320});
  QElapsedTimer clock;
  clock.start();
  REQUIRE(h.add(id, content.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.failures.isEmpty(); }, 10000));
  // One attempt and five retries.
  CHECK(server.requests().size() == 6);
  CHECK(clock.elapsed() >= 20 + 40 + 80 + 160 + 320);
  CHECK(h.failures.front() == qMakePair(id, QueueProblem::Network));
  CHECK(h.queue->status(id)->state == ItemState::Failed);
  CHECK(h.catalog.find(ServerId, UserId, id)->state == ItemState::Failed);

  server.failTimes = 0;
  h.queue->retry(id);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty(); }, 10000));
  CHECK(fileSha(h.mediaPath(id)) == FakeFileServer::sha256(content));
}

TEST_CASE("403 fails at once; 401 stops the queue until the next sign-in") {
  FakeFileServer server;
  const QString first = itemIdFor(5);
  const QString second = itemIdFor(6);
  server.setFile(downloadPath(first), {patternBytes(1000, 6)});
  server.setFile(downloadPath(second), {patternBytes(1000, 7)});
  server.failStatus = 403;
  server.failTimes = 1;

  Harness h(server.url());
  REQUIRE(h.add(first, 1000) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.failures.isEmpty(); }));
  CHECK(h.failures.front().second == QueueProblem::Refused);
  CHECK(server.requests().size() == 1);

  server.failStatus = 401;
  server.failTimes = 1;
  REQUIRE(h.add(second, 1000) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return h.queue->problem() == QueueProblem::SignInRequired; }));
  CHECK(h.queue->status(second)->state == ItemState::Queued);
  h.queue->resumeAll();
  QThread::msleep(50);
  QCoreApplication::processEvents();
  CHECK(server.requests().size() == 2);
  // Signing in again continues.
  h.queue->setAccess(ServerId, UserId, server.url(), "MediaBrowser Token=\"new\"");
  REQUIRE(waitFor([&] { return h.completed.contains(second); }));
  CHECK(server.requests().last().authorization.contains("new"));
}

TEST_CASE("A redirect off the server is not followed, so the token stays home") {
  FakeFileServer elsewhere;
  FakeFileServer server;
  const QString id = itemIdFor(8);
  server.redirectTo = elsewhere.url(downloadPath(id)).toEncoded();
  elsewhere.setFile(downloadPath(id), {patternBytes(1000, 8)});

  Harness h(server.url());
  REQUIRE(h.add(id, 1000) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.failures.isEmpty(); }));
  CHECK(h.failures.front().second == QueueProblem::NotServerUrl);
  CHECK(elsewhere.requests().isEmpty());

  // A job for another host never leaves the worker.
  auto guard = std::make_shared<FakeStorageGuard>(0, 0);
  DownloadWorker worker(guard);
  int outcome = -1;
  QObject::connect(&worker, &DownloadWorker::transferFinished,
                   [&](const QString&, int o, qint64) { outcome = o; });
  DownloadJob job;
  job.key = QStringLiteral("k");
  job.url = elsewhere.url(downloadPath(id));
  job.serverUrl = server.url();
  job.authorization = Token;
  job.partPath = h.dir.filePath(QStringLiteral("x.part"));
  job.finalPath = h.dir.filePath(QStringLiteral("x.bin"));
  worker.start(job);
  CHECK(outcome == static_cast<int>(TransferOutcome::NotServerUrl));
  QThread::msleep(50);
  QCoreApplication::processEvents();
  CHECK(elsewhere.requests().isEmpty());
}

TEST_CASE("After a download the folder holds no token and no ApiKey anywhere") {
  FakeFileServer server;
  const QString id = itemIdFor(9);
  const QByteArray content = patternBytes(256 * 1024, 9);
  server.setFile(downloadPath(id), {content});
  server.cutAfter = 100 * 1024;
  server.cutTimes = 1;

  Harness h(server.url());
  REQUIRE(h.add(id, content.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty(); }, 10000));
  CHECK(filesContaining(h.store.root(),
                        {Token, "apikey", "api_key", "TranscodingUrl", "DirectStreamUrl"})
            .isEmpty());
  // The manifest still has what the offline screens need.
  const auto m = h.store.readManifest(ServerId, UserId, id);
  REQUIRE(m);
  CHECK(m->item.value(QStringLiteral("Name")).toString() == QStringLiteral("Alice in the Test"));
  CHECK(m->userData.value(QStringLiteral("Played")).toBool() == false);
}

TEST_CASE(
    "Space: no start past the reserve or the limit; low space mid-download pauses within 1 s") {
  FakeFileServer server;
  const QString small = itemIdFor(10);
  const QString big = itemIdFor(11);
  const QString slow = itemIdFor(12);
  const QByteArray smallContent = patternBytes(600 * 1024, 10);
  const QByteArray slowContent = patternBytes(4 * MiB, 12);
  server.setFile(downloadPath(small), {smallContent});
  server.setFile(downloadPath(big), {patternBytes(600 * 1024, 11)});
  server.setFile(downloadPath(slow), {slowContent});

  Harness h(server.url());
  QList<QueueProblem> problems;
  QObject::connect(h.queue.get(), &DownloadQueue::problemChanged,
                   [&](QueueProblem p) { problems.append(p); });

  // Reserve: 10 GiB free, 5 GiB kept, a 6 GiB item does not start.
  h.guard->setReserveBytes(5 * GiB);
  h.guard->free = 10 * GiB;
  const QString huge = itemIdFor(13);
  const QJsonObject hugeSource = mediaSourceJson(huge, 6 * GiB);
  REQUIRE(h.queue->enqueueOriginal(itemJson(huge, hugeSource), hugeSource) ==
          EnqueueResult::Queued);
  CHECK(h.queue->problem() == QueueProblem::LowSpace);
  CHECK(server.requests().isEmpty());
  h.queue->cancel(huge);
  CHECK_FALSE(h.catalog.find(ServerId, UserId, huge));

  // Limit: 1 MiB; a 600 KiB item fits, the next 600 KiB does not.
  h.guard->free = 1 << 30;
  h.guard->setReserveBytes(0);
  h.guard->setLimitBytes(MiB);
  h.queue->resumeAll();
  REQUIRE(h.add(small, smallContent.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return h.completed.contains(small); }));
  REQUIRE(h.add(big, 600 * 1024) == EnqueueResult::Queued);
  CHECK(h.queue->problem() == QueueProblem::OverLimit);
  CHECK(server.requests().size() == 1);
  h.queue->cancel(big);

  // Mid-download: the disk fills while a slow transfer runs.
  h.guard->setLimitBytes(0);
  h.guard->setReserveBytes(GiB);
  h.guard->free = 100 * GiB;
  h.queue->setCheckEveryBytes(256 * 1024);
  server.chunkBytes = 64 * 1024;
  server.chunkIntervalMs = 20;
  h.queue->resumeAll();
  REQUIRE(h.add(slow, slowContent.size()) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return h.queue->status(slow)->bytesOnDisk > 512 * 1024; }, 10000));
  QElapsedTimer clock;
  clock.start();
  h.guard->free = GiB / 2;
  REQUIRE(waitFor([&] { return h.queue->problem() == QueueProblem::LowSpace; }, 5000));
  CHECK(clock.elapsed() < 1000);
  REQUIRE(waitFor([&] { return h.queue->activeCount() == 0; }));
  CHECK(h.queue->status(slow)->state == ItemState::Queued);

  // Space again: it resumes where it stopped.
  h.guard->free = 100 * GiB;
  server.chunkBytes = 0;
  h.queue->resumeAll();
  REQUIRE(waitFor([&] { return h.completed.contains(slow); }, 10000));
  CHECK(fileSha(h.mediaPath(slow)) == FakeFileServer::sha256(slowContent));
  CHECK(server.requests().last().status == 206);
}

TEST_CASE("Pause, resume, reorder and cancel; the order survives a restart") {
  FakeFileServer server;
  server.chunkBytes = 16 * 1024;
  server.chunkIntervalMs = 20;
  QStringList ids;
  for (int i = 20; i < 23; ++i) {
    ids.append(itemIdFor(i));
    server.setFile(downloadPath(ids.last()), {patternBytes(512 * 1024, static_cast<quint32>(i))});
  }

  auto h = std::make_unique<Harness>(server.url());
  for (const QString& id : ids)
    REQUIRE(h->add(id, 512 * 1024) == EnqueueResult::Queued);
  CHECK(h->add(ids[0], 512 * 1024) == EnqueueResult::AlreadyThere);
  REQUIRE(waitFor([&] { return h->queue->status(ids[0])->bytesOnDisk > 0; }));
  CHECK(h->queue->activeCount() == 1);

  h->queue->pause(ids[0]);
  REQUIRE(waitFor([&] { return h->queue->status(ids[0])->state == ItemState::Paused; }));
  // The next one starts; the last moves to the front for later.
  REQUIRE(waitFor([&] { return h->queue->status(ids[1])->activity == Activity::Transferring; }));
  h->queue->move(ids[2], 0);
  CHECK(h->queue->order() == QStringList{ids[2], ids[0], ids[1]});
  h->queue->cancel(ids[1]);
  REQUIRE(waitFor([&] { return !h->queue->status(ids[1]); }));
  CHECK_FALSE(QDir(h->store.itemDir(ServerId, UserId, ids[1])).exists());

  // Restart: same folder, new queue.
  const QString root = h->dir.path();
  h->dir.setAutoRemove(false);
  h.reset();
  server.chunkBytes = 0;
  DownloadStore store(root + QStringLiteral("/downloads"));
  OfflineCatalog catalog(store);
  catalog.load();
  auto guard = std::make_shared<FakeStorageGuard>(0, 0);
  DownloadQueue queue(store, catalog, guard);
  queue.setAccess(ServerId, UserId, server.url(), "MediaBrowser Token=\"t\"");
  CHECK(queue.order() == QStringList{ids[2], ids[0]});
  CHECK(queue.status(ids[0])->state == ItemState::Paused);
  REQUIRE(waitFor(
      [&] { return catalog.find(ServerId, UserId, ids[2])->state == ItemState::Complete; }));
  queue.resume(ids[0]);
  REQUIRE(waitFor(
      [&] { return catalog.find(ServerId, UserId, ids[0])->state == ItemState::Complete; }));
  CHECK(fileSha(store.mediaPath(*catalog.find(ServerId, UserId, ids[0]))) ==
        FakeFileServer::sha256(patternBytes(512 * 1024, 20)));
  QDir(root).removeRecursively();
}

TEST_CASE("Another user's downloads stay on disk and are not resumed") {
  FakeFileServer server;
  const QString id = itemIdFor(30);
  server.setFile(downloadPath(id), {patternBytes(1000, 30)});
  server.failStatus = 503;
  server.failTimes = -1;
  Harness h(server.url());
  h.queue->setRetryDelaysMs({});
  REQUIRE(h.add(id, 1000) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.failures.isEmpty(); }));
  h.queue->retry(id);
  h.queue->pauseAll();
  // Signing out keeps the downloads; another user does not see them in the queue.
  h.queue->setAccess(ServerId, QStringLiteral("b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0"), server.url(),
                     "x");
  CHECK(h.queue->order().isEmpty());
  CHECK(h.catalog.find(ServerId, UserId, id));
  h.queue->setAccess({}, {}, {}, {});
  CHECK(QFile::exists(h.store.manifestPath(*h.catalog.find(ServerId, UserId, id))));
}
