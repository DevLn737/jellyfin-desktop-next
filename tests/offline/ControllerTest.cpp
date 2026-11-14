#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "FakeFileServer.h"
#include "OfflineFixtures.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/offline/DownloadListModel.h>
#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/DownloadsController.h>
#include <jfd/offline/OfflineCatalog.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

// The lists and actions behind the download screens (DownloadsController):
// the server's permission, what "Download" on a series, an album or a film
// takes and the total shown before it is queued, the queue's actions and
// how its list moves, the downloaded list with its filter, the space
// numbers and deleting.

using namespace jfd;
using namespace jfd::offline;
using namespace jfd::test;

namespace {

  api::ClientIdentity identity() {
    return api::ClientIdentity{QStringLiteral("Test Client"), QStringLiteral("1.0"),
                               QStringLiteral("Test Device"), QStringLiteral("device1")};
  }

  const QString SeriesId = QStringLiteral("5e1e5000000000000000000000000001");
  const QString AlbumId = QStringLiteral("a1b0a000000000000000000000000001");

  QJsonObject episode(int n, qint64 size, bool played = false, bool strm = false) {
    const QString id = itemIdFor(100 + n);
    QJsonObject source = mediaSourceJson(id, size);
    if (strm) source.insert(QStringLiteral("Path"), QStringLiteral("/media/remote/episode.strm"));
    return QJsonObject{
        {QStringLiteral("Id"), id},
        {QStringLiteral("Name"), QStringLiteral("Episode %1").arg(n)},
        {QStringLiteral("Type"), QStringLiteral("Episode")},
        {QStringLiteral("MediaType"), QStringLiteral("Video")},
        {QStringLiteral("VideoType"), QStringLiteral("VideoFile")},
        {QStringLiteral("SeriesId"), SeriesId},
        {QStringLiteral("SeriesName"), QStringLiteral("Bob and the Tests")},
        {QStringLiteral("ParentIndexNumber"), 1},
        {QStringLiteral("IndexNumber"), n},
        {QStringLiteral("RunTimeTicks"), 18000000000.0},
        {QStringLiteral("UserData"), QJsonObject{{QStringLiteral("Played"), played}}},
        {QStringLiteral("MediaSources"), QJsonArray{source}},
    };
  }

  QJsonObject track(int n, qint64 size, bool played = false) {
    const QString id = itemIdFor(200 + n);
    QJsonObject source = mediaSourceJson(id, size, QStringLiteral("flac"));
    source.remove(QStringLiteral("VideoType"));
    return QJsonObject{
        {QStringLiteral("Id"), id},
        {QStringLiteral("Name"), QStringLiteral("Song %1").arg(n)},
        {QStringLiteral("Type"), QStringLiteral("Audio")},
        {QStringLiteral("MediaType"), QStringLiteral("Audio")},
        {QStringLiteral("AlbumId"), AlbumId},
        {QStringLiteral("Album"), QStringLiteral("Alice Sings")},
        {QStringLiteral("AlbumArtist"), QStringLiteral("Alice")},
        {QStringLiteral("IndexNumber"), n},
        {QStringLiteral("UserData"), QJsonObject{{QStringLiteral("Played"), played}}},
        {QStringLiteral("MediaSources"), QJsonArray{source}},
    };
  }

  // The server's answers: the user, items by id, a folder's children.
  struct FakeJellyfin {
    bool mayDownload = true;
    QHash<QString, QJsonObject> items;
    QHash<QString, QJsonArray> children;
    QList<QUrlQuery> childQueries;
    FakeServer server{[this](const HttpRequest& r) { return handle(r); }};
    api::JellyfinClient client{identity()};

    FakeJellyfin() {
      client.setServerUrl(server.url());
      client.setSession(QString::fromLatin1(Token), UserId);
    }

    HttpResponse handle(const HttpRequest& r) {
      const auto json = [](const QJsonObject& o) { return HttpResponse{200, QJsonDocument(o).toJson()}; };
      if (r.path == QLatin1String("/Users/Me"))
        return json({{QStringLiteral("Id"), UserId},
                     {QStringLiteral("Name"), QStringLiteral("alice")},
                     {QStringLiteral("ServerId"), ServerId},
                     {QStringLiteral("Policy"),
                      QJsonObject{{QStringLiteral("EnableContentDownloading"), mayDownload}}}});
      if (r.path == QLatin1String("/Items")) {
        QJsonArray found;
        const QString parent = r.query.queryItemValue(QStringLiteral("ParentId"));
        if (!parent.isEmpty()) {
          childQueries.append(r.query);
          found = children.value(parent);
          if (r.query.queryItemValue(QStringLiteral("Filters")) == QLatin1String("IsUnplayed")) {
            QJsonArray unplayed;
            for (const QJsonValue& v : std::as_const(found))
              if (!v.toObject().value(QStringLiteral("UserData")).toObject().value(QStringLiteral("Played")).toBool())
                unplayed.append(v);
            found = unplayed;
          }
        } else {
          for (const QString& id : r.query.queryItemValue(QStringLiteral("ids")).split(QLatin1Char(',')))
            if (items.contains(id)) found.append(items[id]);
        }
        return json({{QStringLiteral("Items"), found}});
      }
      return {404, "{}"};
    }
  };

  // The services of the app around a temporary download folder. The queue
  // has no server address unless one is given, so nothing transfers.
  struct Harness {
    QTemporaryDir dir;
    DownloadStore store{dir.filePath(QStringLiteral("downloads"))};
    OfflineCatalog catalog{store};
    std::shared_ptr<FakeStorageGuard> guard = std::make_shared<FakeStorageGuard>(0, 0);
    std::unique_ptr<DownloadQueue> queue;
    std::unique_ptr<DownloadsController> controller;

    explicit Harness(const QUrl& fileServer = {}) {
      catalog.load();
      queue = std::make_unique<DownloadQueue>(store, catalog, guard);
      queue->setAccess(ServerId, UserId, fileServer, "MediaBrowser Token=\"" + Token + "\"");
      controller = std::make_unique<DownloadsController>(store, catalog, *queue, *guard);
      controller->setUser(ServerId, UserId);
    }

    // A complete download of `item`, as the queue leaves it.
    void addComplete(const QJsonObject& item, qint64 bytes, QStringList parents = {}) {
      ItemManifest m;
      m.serverId = ServerId;
      m.userId = UserId;
      m.itemId = item.value(QStringLiteral("Id")).toString();
      m.state = ItemState::Complete;
      m.item = item;
      m.mediaSource = item.value(QStringLiteral("MediaSources")).toArray().first().toObject();
      m.userData = item.value(QStringLiteral("UserData")).toObject();
      m.mediaFile = QStringLiteral("media.mkv");
      m.expectedSize = bytes;
      m.parents = std::move(parents);
      m.queuedAt = m.completedAt = QDateTime::currentDateTimeUtc();
      REQUIRE(store.writeManifest(m));
      QFile file(store.mediaPath(m));
      REQUIRE(file.open(QIODevice::WriteOnly));
      file.write(QByteArray(int(bytes), 'x'));
      catalog.update(m);
    }

    QVariantMap prepare(const QString& itemId, bool unplayedOnly = false, int* requestId = nullptr) {
      QSignalSpy prepared(controller.get(), &DownloadsController::prepared);
      QSignalSpy failed(controller.get(), &DownloadsController::prepareFailed);
      const int id = controller->prepare(itemId, unplayedOnly);
      if (requestId) *requestId = id;
      REQUIRE(waitFor([&] { return prepared.count() + failed.count() > 0; }));
      REQUIRE_MESSAGE(failed.isEmpty(), (failed.isEmpty() ? std::string() : failed.first().at(1).toString().toStdString()));
      CHECK(prepared.first().at(0).toInt() == id);
      return prepared.first().at(1).toMap();
    }

    QStringList ids(const DownloadListModel& model) const {
      QStringList out;
      for (int i = 0; i < model.count(); ++i) out.append(model.itemIdAt(i));
      return out;
    }
  };

  void allowDownloads(Harness& h, FakeJellyfin& server) {
    h.controller->setClient(&server.client);
    h.controller->setOnline(true);
    REQUIRE(waitFor([&] { return h.controller->permissionKnown(); }));
  }

} // namespace

TEST_CASE("Without EnableContentDownloading there is no download, and the reason is given") {
  FakeJellyfin server;
  server.mayDownload = false;
  server.items.insert(itemIdFor(1), itemJson(itemIdFor(1), mediaSourceJson(itemIdFor(1), 1000)));
  Harness h;
  CHECK_FALSE(h.controller->permissionKnown());
  CHECK_FALSE(h.controller->allowed());
  h.controller->setClient(&server.client);
  // Offline nothing is asked.
  QCoreApplication::processEvents();
  CHECK_FALSE(h.controller->permissionKnown());
  allowDownloads(h, server);
  CHECK_FALSE(h.controller->allowed());

  QSignalSpy failed(h.controller.get(), &DownloadsController::prepareFailed);
  h.controller->prepare(itemIdFor(1));
  REQUIRE(waitFor([&] { return failed.count() == 1; }));
  CHECK(failed.first().at(1).toString().contains(QStringLiteral("may not download")));
  CHECK(h.queue->order().isEmpty());

  // Another user signs in: asked again.
  server.mayDownload = true;
  h.controller->setUser(ServerId, QStringLiteral("b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0"));
  CHECK_FALSE(h.controller->permissionKnown());
  REQUIRE(waitFor([&] { return h.controller->permissionKnown(); }));
  CHECK(h.controller->allowed());
}

TEST_CASE("A series shows its total first, then queues the episodes that can be downloaded") {
  FakeJellyfin server;
  server.items.insert(SeriesId, QJsonObject{{QStringLiteral("Id"), SeriesId},
                                            {QStringLiteral("Name"), QStringLiteral("Bob and the Tests")},
                                            {QStringLiteral("Type"), QStringLiteral("Series")},
                                            {QStringLiteral("IsFolder"), true}});
  const QJsonObject e1 = episode(1, 1000, true);
  const QJsonObject e2 = episode(2, 5000, false, /*strm*/ true);
  const QJsonObject e3 = episode(3, 100 * MiB);
  const QJsonObject e4 = episode(4, 200 * MiB, true);
  server.children.insert(SeriesId, QJsonArray{e1, e2, e3, e4});

  Harness h;
  h.addComplete(e1, 1000);
  h.controller->refresh();
  allowDownloads(h, server);
  REQUIRE(h.controller->allowed());
  CHECK(h.controller->stateOf(SeriesId) == QStringLiteral("complete"));

  int request = 0;
  QVariantMap summary = h.prepare(SeriesId, false, &request);
  CHECK(summary.value(QStringLiteral("title")).toString() == QStringLiteral("Bob and the Tests"));
  CHECK(summary.value(QStringLiteral("count")).toInt() == 2);
  CHECK(summary.value(QStringLiteral("alreadyCount")).toInt() == 1);
  CHECK(summary.value(QStringLiteral("unavailableCount")).toInt() == 1);
  CHECK(summary.value(QStringLiteral("bytes")).toLongLong() == 300 * MiB);
  CHECK_FALSE(summary.value(QStringLiteral("single")).toBool());
  CHECK(summary.value(QStringLiteral("fits")).toBool());
  // Recursive, playable items only, in season and episode order.
  REQUIRE(server.childQueries.size() == 1);
  const QUrlQuery& q = server.childQueries.first();
  CHECK(q.queryItemValue(QStringLiteral("Recursive")) == QStringLiteral("true"));
  CHECK(q.queryItemValue(QStringLiteral("IncludeItemTypes")).contains(QStringLiteral("Episode")));
  CHECK(q.queryItemValue(QStringLiteral("SortBy")).startsWith(QStringLiteral("ParentIndexNumber")));
  CHECK(q.queryItemValue(QStringLiteral("Fields")).contains(QStringLiteral("MediaSources")));
  CHECK_FALSE(q.hasQueryItem(QStringLiteral("Filters")));
  // Nothing is queued before the user agrees.
  CHECK(h.queue->order().isEmpty());

  // Only the unwatched ones: e4 drops out.
  h.controller->discard(request);
  summary = h.prepare(SeriesId, true, &request);
  CHECK(server.childQueries.last().queryItemValue(QStringLiteral("Filters")) == QStringLiteral("IsUnplayed"));
  CHECK(summary.value(QStringLiteral("count")).toInt() == 1);
  CHECK(summary.value(QStringLiteral("bytes")).toLongLong() == 100 * MiB);

  h.controller->discard(request);
  summary = h.prepare(SeriesId, false, &request);
  CHECK(h.controller->confirm(request) == 2);
  // A request is used once.
  CHECK(h.controller->confirm(request) == 0);
  const QString id3 = e3.value(QStringLiteral("Id")).toString();
  const QString id4 = e4.value(QStringLiteral("Id")).toString();
  CHECK(h.queue->order() == QStringList{id3, id4});
  CHECK(h.ids(*h.controller->queueModel()) == QStringList{id3, id4});
  // The series and its episodes know each other: the badge of the series
  // card, and the manifests' parents (also of the one already there).
  CHECK(h.controller->stateOf(SeriesId) == QStringLiteral("queued"));
  CHECK(h.controller->stateOf(id3) == QStringLiteral("queued"));
  CHECK(h.catalog.find(ServerId, UserId, id3)->parents == QStringList{SeriesId});
  CHECK(h.catalog.find(ServerId, UserId, e1.value(QStringLiteral("Id")).toString())->parents ==
        QStringList{SeriesId});
  const QVariantMap group = h.controller->groupSummary(SeriesId);
  CHECK(group.value(QStringLiteral("count")).toInt() == 3);
  CHECK(group.value(QStringLiteral("complete")).toInt() == 1);

  // The queue row says what it is.
  const DownloadListModel& queue = *h.controller->queueModel();
  const QModelIndex first = queue.index(0);
  CHECK(queue.data(first, DownloadListModel::SubtitleRole).toString() ==
        QStringLiteral("Bob and the Tests · S1:E3"));
  CHECK(queue.data(first, DownloadListModel::StateRole).toString() == QStringLiteral("queued"));
  CHECK(queue.data(first, DownloadListModel::TotalBytesRole).toLongLong() == 100 * MiB);
}

TEST_CASE("A film with two versions offers both, and the chosen one is downloaded") {
  FakeJellyfin server;
  const QString id = itemIdFor(7);
  QJsonObject first = mediaSourceJson(QStringLiteral("50c0000000000000000000000000000a"), 4 * GiB);
  first.insert(QStringLiteral("Name"), QStringLiteral("4K"));
  QJsonObject second = mediaSourceJson(QStringLiteral("50c0000000000000000000000000000b"), GiB);
  second.insert(QStringLiteral("Name"), QStringLiteral("1080p"));
  QJsonObject item = itemJson(id, first);
  item.insert(QStringLiteral("MediaSources"), QJsonArray{first, second});
  server.items.insert(id, item);

  Harness h;
  allowDownloads(h, server);
  int request = 0;
  const QVariantMap summary = h.prepare(id, false, &request);
  CHECK(summary.value(QStringLiteral("single")).toBool());
  CHECK(summary.value(QStringLiteral("count")).toInt() == 1);
  const QVariantList versions = summary.value(QStringLiteral("versions")).toList();
  REQUIRE(versions.size() == 2);
  CHECK(versions[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("4K"));
  CHECK(versions[1].toMap().value(QStringLiteral("bytes")).toLongLong() == GiB);

  CHECK(h.controller->confirm(request, 1) == 1);
  const auto m = h.catalog.find(ServerId, UserId, id);
  REQUIRE(m);
  CHECK(m->mediaSource.value(QStringLiteral("Id")).toString() == QStringLiteral("50c0000000000000000000000000000b"));
  CHECK(m->parents.isEmpty());
  // Secrets of the server's answer never reach the disk.
  CHECK(filesContaining(h.store.root(), {Token}).isEmpty());
}

TEST_CASE("The total says when it would not fit the space for downloads or the disk") {
  FakeJellyfin server;
  const QString id = itemIdFor(8);
  server.items.insert(id, itemJson(id, mediaSourceJson(id, 2 * GiB)));
  Harness h;
  allowDownloads(h, server);

  h.guard->setLimitBytes(GiB);
  QVariantMap summary = h.prepare(id);
  CHECK_FALSE(summary.value(QStringLiteral("fits")).toBool());
  CHECK(summary.value(QStringLiteral("spaceText")).toString().contains(QStringLiteral("space for downloads")));

  h.guard->setLimitBytes(0);
  h.guard->setReserveBytes(5 * GiB);
  h.guard->free = 6 * GiB;
  summary = h.prepare(id);
  CHECK_FALSE(summary.value(QStringLiteral("fits")).toBool());
  CHECK(summary.value(QStringLiteral("spaceText")).toString().contains(QStringLiteral("free")));

  h.guard->free = 100 * GiB;
  summary = h.prepare(id);
  CHECK(summary.value(QStringLiteral("fits")).toBool());
  CHECK(summary.value(QStringLiteral("spaceText")).toString().isEmpty());
}

TEST_CASE("Queue actions move, pause and cancel rows without resetting the list") {
  Harness h;
  QStringList ids;
  for (int n = 1; n <= 3; ++n) {
    const QString id = itemIdFor(30 + n);
    const QJsonObject source = mediaSourceJson(id, 1000 * n);
    REQUIRE(h.queue->enqueueOriginal(itemJson(id, source), source) == EnqueueResult::Queued);
    ids.append(id);
  }
  DownloadListModel& model = *h.controller->queueModel();
  REQUIRE(waitFor([&] { return model.count() == 3; }));
  QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
  QSignalSpy moves(&model, &QAbstractItemModel::rowsMoved);

  h.controller->moveDown(ids[0]);
  REQUIRE(waitFor([&] { return h.ids(model) == QStringList{ids[1], ids[0], ids[2]}; }));
  CHECK(moves.count() >= 1);
  h.controller->moveUp(ids[2]);
  REQUIRE(waitFor([&] { return h.ids(model) == QStringList{ids[1], ids[2], ids[0]}; }));
  // Already first: stays.
  h.controller->moveUp(ids[1]);
  QCoreApplication::processEvents();
  CHECK(h.ids(model).first() == ids[1]);

  h.controller->pause(ids[2]);
  REQUIRE(waitFor([&] { return h.controller->stateOf(ids[2]) == QStringLiteral("paused"); }));
  const int row = model.rowOf(ids[2]);
  REQUIRE(waitFor([&] {
    return model.data(model.index(row), DownloadListModel::StateRole).toString() == QStringLiteral("paused");
  }));
  CHECK(model.data(model.index(row), DownloadListModel::StateRole).toString() == QStringLiteral("paused"));
  CHECK(model.data(model.index(row), DownloadListModel::StatusTextRole).toString() == QStringLiteral("Paused"));
  h.controller->resume(ids[2]);
  REQUIRE(waitFor([&] { return h.controller->stateOf(ids[2]) == QStringLiteral("queued"); }));

  h.controller->cancel(ids[1]);
  REQUIRE(waitFor([&] { return h.ids(model) == QStringList{ids[2], ids[0]}; }));
  CHECK_FALSE(h.catalog.find(ServerId, UserId, ids[1]));
  CHECK(resets.isEmpty());

  // The whole queue: paused by the user, then continued.
  h.controller->pauseAll();
  REQUIRE(waitFor([&] { return h.controller->queuePausedByUser(); }));
  CHECK(h.controller->queueStopped());
  CHECK_FALSE(h.controller->queueProblemText().isEmpty());
  h.controller->resumeAll();
  REQUIRE(waitFor([&] { return !h.controller->queueStopped(); }));
}

TEST_CASE("Downloaded items: the list, its filter, space, and deleting watched, a group and all") {
  Harness h;
  h.guard->free = 50 * GiB;
  h.guard->setLimitBytes(100 * GiB);
  const QJsonObject s1 = track(1, 3000, /*played*/ true);
  const QJsonObject s2 = track(2, 4000);
  const QJsonObject e1 = episode(1, 5000, /*played*/ true);
  const QJsonObject e2 = episode(2, 6000);
  h.addComplete(s1, 3000);
  h.addComplete(s2, 4000);
  h.addComplete(e1, 5000);
  h.addComplete(e2, 6000);
  h.controller->refresh();

  DownloadListModel& model = *h.controller->downloadedModel();
  // By album or series, then number.
  CHECK(h.ids(model) == QStringList{s1.value(QStringLiteral("Id")).toString(), s2.value(QStringLiteral("Id")).toString(),
                                     e1.value(QStringLiteral("Id")).toString(), e2.value(QStringLiteral("Id")).toString()});
  CHECK(h.controller->hasDownloads());
  CHECK(h.controller->usedBytes() == 18000);
  CHECK(h.controller->watchedBytes() == 8000);
  CHECK(h.controller->freeBytes() == 50 * GiB);
  CHECK(h.controller->limitBytes() == 100 * GiB);
  CHECK(model.data(model.index(0), DownloadListModel::SubtitleRole).toString() == QStringLiteral("Alice · Alice Sings"));
  CHECK(model.data(model.index(0), DownloadListModel::StateRole).toString() == QStringLiteral("complete"));
  CHECK(model.data(model.index(0), DownloadListModel::ProgressRole).toDouble() == 1.0);
  CHECK(h.controller->stateOf(AlbumId) == QStringLiteral("complete"));
  CHECK(h.controller->bytesOf(AlbumId) == 7000);

  model.setFilterText(QStringLiteral("bob"));
  CHECK(model.count() == 2);
  model.setFilterText(QStringLiteral("song 2"));
  CHECK(h.ids(model) == QStringList{s2.value(QStringLiteral("Id")).toString()});
  model.setFilterText(QString());
  CHECK(model.count() == 4);

  CHECK(h.controller->removeWatched() == 2);
  CHECK(model.count() == 2);
  CHECK(h.controller->watchedBytes() == 0);
  CHECK_FALSE(QFile::exists(h.store.itemDir(ServerId, UserId, e1.value(QStringLiteral("Id")).toString())));

  CHECK(h.controller->remove(AlbumId));
  CHECK(h.controller->stateOf(AlbumId).isEmpty());
  CHECK(h.ids(model) == QStringList{e2.value(QStringLiteral("Id")).toString()});

  CHECK(h.controller->removeAll() == 1);
  CHECK(model.count() == 0);
  CHECK_FALSE(h.controller->hasDownloads());
  CHECK(h.controller->usedBytes() == 0);
  CHECK(h.catalog.items(ServerId, UserId).isEmpty());
}

TEST_CASE("A download moves from the queue to the downloaded list, with progress on the way") {
  FakeFileServer files;
  const QString id = itemIdFor(50);
  const QByteArray content = patternBytes(2 * MiB, 50);
  files.setFile(QStringLiteral("/Items/") + id + QStringLiteral("/Download"), {content});
  Harness h(files.url());
  QSignalSpy progress(h.controller.get(), &DownloadsController::progressRevisionChanged);
  const QJsonObject source = mediaSourceJson(id, content.size());
  REQUIRE(h.queue->enqueueOriginal(itemJson(id, source), source) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return h.controller->stateOf(id) == QStringLiteral("complete"); }, 15000));
  REQUIRE(waitFor([&] { return h.controller->downloadedModel()->count() == 1; }));
  CHECK(h.controller->queueModel()->count() == 0);
  CHECK(h.controller->progressOf(id) == 1.0);
  CHECK(progress.count() >= 1);
  CHECK(h.controller->usedBytes() == content.size());
  CHECK(DownloadsController::formatBytesText(2 * MiB) == QStringLiteral("2 MiB"));
  CHECK(DownloadsController::formatBytesText(3 * GiB / 2).contains(QStringLiteral("GiB")));
}
