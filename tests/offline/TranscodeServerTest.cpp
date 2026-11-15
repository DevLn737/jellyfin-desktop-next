#include "../support/DoctestQt.h"
#include "../support/ExtrasStand.h"
#include "../support/FakeServer.h"
#include "OfflineFixtures.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/OfflineCatalog.h>

#include <MpvPlayer.h>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <atomic>
#include <cmath>
#include <memory>
#include <optional>

// Test 15 of package 3.10 (stage3-briefs-3.9-3.10.md, "Загрузки: сжатая
// копия") on the extras stand (scripts/test-server/extras-server.sh,
// JFD_EXTRAS_*; skipped without it): the two-minute clip downloaded as a
// compressed copy through the server's HLS transcode plays in headless mpv
// with its duration within 1 s and a seek to 50 % under 2 s; a download cut
// off mid-way (the app closed, the server's job stopped) continues after the
// last whole segment in a new play session and still gives the whole clip;
// a music track compressed to Opus keeps its length.

using namespace jfd;
using namespace jfd::api;
using namespace jfd::offline;
using jfd::test::hasExtrasStand;
using jfd::test::waitFor;

namespace {
  constexpr double TicksPerSecond = 10'000'000.0;

  template <class T> Result<T> run(std::function<void(Callback<T>)> call, int timeoutMs = 20000) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    if (!waitFor([&] { return out.has_value(); }, timeoutMs)) {
      ApiError error;
      error.kind = ApiError::Kind::Timeout;
      return error;
    }
    return std::move(*out);
  }

  struct Stand {
    std::unique_ptr<JellyfinClient> client;
    QString serverId;
    QTemporaryDir dir;
    DownloadStore store{dir.filePath(QStringLiteral("downloads"))};
    OfflineCatalog catalog{store};
    std::shared_ptr<StorageGuard> guard = std::make_shared<StorageGuard>(0, 0);
    std::unique_ptr<DownloadQueue> queue;
    QStringList completed;
    QList<QueueProblem> failures;

    Stand() {
      client = std::make_unique<JellyfinClient>(ClientIdentity{
          QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"),
          QStringLiteral("CI offline transcode"), QStringLiteral("ci-offline-transcode")});
      const auto url = JellyfinClient::normalizeServerUrl(qEnvironmentVariable("JFD_EXTRAS_SERVER_URL"));
      REQUIRE(url.has_value());
      client->setServerUrl(*url);
      auto auth = run<AuthenticationResult>([&](auto done) {
        client->signIn(qEnvironmentVariable("JFD_EXTRAS_USER"), qEnvironmentVariable("JFD_EXTRAS_PASSWORD"),
                       nullptr, done);
      });
      REQUIRE(auth.ok());
      serverId = auth->serverId;
      openQueue();
    }

    void openQueue() {
      queue.reset();
      catalog.load();
      queue = std::make_unique<DownloadQueue>(store, catalog, guard);
      QObject::connect(queue.get(), &DownloadQueue::completed,
                       [this](const QString& id) { completed.append(id); });
      QObject::connect(queue.get(), &DownloadQueue::failed,
                       [this](const QString&, QueueProblem p) { failures.append(p); });
      queue->setAccess(serverId, client->userId(), client->serverUrl(), client->authorizationHeader());
    }

    // The server's JSON of the one item of `type` matching `search`.
    QJsonObject find(const QString& type, const QString& search) {
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("userId"), client->userId());
      query.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
      query.addQueryItem(QStringLiteral("includeItemTypes"), type);
      query.addQueryItem(QStringLiteral("searchTerm"), search);
      query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("MediaSources"));
      auto page = run<QJsonObject>([&](auto done) {
        client->sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt, nullptr,
                         done);
      });
      REQUIRE(page.ok());
      const QJsonArray items = page->value(QStringLiteral("Items")).toArray();
      REQUIRE(items.size() == 1);
      return items.first().toObject();
    }

    std::optional<ItemManifest> manifest(const QString& id) const {
      return store.readManifest(serverId, client->userId(), id);
    }
  };

  // Plenty of free space until the download has asked `tripAfter` times
  // while writing, then none: the queue stops the transfer there, after a
  // whole segment, as deterministically as a test can cut a download off.
  class CuttingGuard : public StorageGuard {
  public:
    explicit CuttingGuard(int tripAfter) : StorageGuard(0, GiB), m_tripAfter(tripAfter) {}
    std::optional<std::int64_t> freeBytes(const QString&) const override {
      return ++m_queries > m_tripAfter ? 0 : std::int64_t{1} << 40;
    }

  private:
    int m_tripAfter;
    mutable std::atomic<int> m_queries{0};
  };

  QJsonObject firstSource(const QJsonObject& item) {
    return item.value(QStringLiteral("MediaSources")).toArray().first().toObject();
  }

  // Opens `path` in headless mpv: its duration, and how long a seek to the
  // middle takes.
  struct Probe {
    double duration = 0;
    double seekMs = -1;
    double positionAfterSeek = -1;
  };

  Probe probe(const QString& path) {
    media::MpvPlayer::Options options;
    options.audioOutput = QStringLiteral("null");
    options.hwdec = QStringLiteral("no");
    options.extra = {{"vo", "null"}};
    media::MpvPlayer mpv(options);
    REQUIRE(mpv.isValid());
    QSignalSpy first(&mpv, &media::MpvPlayer::firstFrame);
    QSignalSpy seeked(&mpv, &media::MpvPlayer::seekCompleted);
    mpv.load(path);
    REQUIRE(waitFor([&] { return first.count() > 0 && mpv.duration() > 0; }, 20000));
    mpv.setPaused(true);
    Probe result;
    result.duration = mpv.duration();
    mpv.seek(result.duration / 2);
    REQUIRE(waitFor([&] { return seeked.count() > 0; }, 10000));
    result.seekMs = seeked.first().at(0).toDouble();
    waitFor([&] { return std::abs(mpv.position() - result.duration / 2) < 0.5; }, 2000);
    result.positionAfterSeek = mpv.position();
    mpv.stop();
    return result;
  }

} // namespace

TEST_CASE("Test 15: a compressed copy of the 2-minute clip: duration within 1 s, a seek to 50 % under 2 s") {
  if (!hasExtrasStand()) return;
  Stand s;
  const QJsonObject item = s.find(QStringLiteral("Video"), QStringLiteral("Offline Clip"));
  const QString id = item.value(QStringLiteral("Id")).toString();
  const double runTime = item.value(QStringLiteral("RunTimeTicks")).toDouble() / TicksPerSecond;
  REQUIRE(runTime == doctest::Approx(120).epsilon(0.02));

  // The second audio track (rus): the copy has that one only.
  QElapsedTimer timer;
  timer.start();
  REQUIRE(s.queue->enqueueCompressed(item, firstSource(item), QStringLiteral("video-720p"), 2) ==
          EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !s.completed.isEmpty() || !s.failures.isEmpty(); }, 240000));
  REQUIRE(s.failures.isEmpty());
  MESSAGE("downloaded in " << timer.elapsed() << " ms");

  const auto m = s.manifest(id);
  REQUIRE(m);
  CHECK(m->state == ItemState::Complete);
  CHECK(m->quality == QStringLiteral("compressed"));
  CHECK(m->transcode.preset == QStringLiteral("video-720p"));
  CHECK(m->transcode.audioStreamIndex == 2);
  CHECK(m->transcode.videoCodec == QStringLiteral("hevc"));
  // 21 segments of 6 s (the last one short).
  CHECK(m->transcode.segmentCount == 21);
  CHECK(jfd::test::filesContaining(s.store.root(), {s.client->accessToken().toLatin1(), "apikey", "api_key",
                                                    "TranscodingUrl", "DirectStreamUrl", "PlaySessionId"})
            .isEmpty());

  const Probe p = probe(s.store.mediaPath(*m));
  MESSAGE("duration " << p.duration << " s, seek " << p.seekMs << " ms");
  CHECK(std::abs(p.duration - runTime) <= 1.0);
  CHECK(p.seekMs >= 0);
  CHECK(p.seekMs < 2000);
  CHECK(std::abs(p.positionAfterSeek - p.duration / 2) < 1.0);
}

TEST_CASE("Test 15: cut off mid-way, the copy continues after the last whole segment in a new play session") {
  if (!hasExtrasStand()) return;
  Stand s;
  const QJsonObject item = s.find(QStringLiteral("Video"), QStringLiteral("Offline Clip"));
  const QString id = item.value(QStringLiteral("Id")).toString();
  // Free space "runs out" after about 2 MiB of segments.
  s.guard = std::make_shared<CuttingGuard>(4);
  s.openQueue();
  s.queue->setCheckEveryBytes(512 * 1024);
  REQUIRE(s.queue->enqueueCompressed(item, firstSource(item), QStringLiteral("video-1080p")) ==
          EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return s.queue->problem() == QueueProblem::LowSpace || !s.completed.isEmpty(); },
                  240000));
  REQUIRE(s.completed.isEmpty());

  // The transfer stopped (and with it the server's job for its play
  // session, as the server itself ends it after a dropped connection); the
  // app quits and starts again with room on the disk.
  s.queue.reset();
  const auto cut = s.manifest(id);
  REQUIRE(cut);
  REQUIRE(cut->state != ItemState::Complete);
  const qint64 kept = cut->transcode.partBytes;
  const int keptSegments = cut->transcode.segmentsDone;
  REQUIRE(keptSegments >= 2);
  REQUIRE(keptSegments < cut->transcode.segmentCount);
  s.guard = std::make_shared<StorageGuard>(0, 0);
  s.openQueue();
  std::optional<qint64> firstProgress;
  QObject::connect(s.queue.get(), &DownloadQueue::progress, [&](const QString&, qint64 bytes, qint64) {
    if (!firstProgress) firstProgress = bytes;
  });
  REQUIRE(waitFor([&] { return !s.completed.isEmpty() || !s.failures.isEmpty(); }, 240000));
  REQUIRE(s.failures.isEmpty());
  // It went on from what was on disk instead of starting over.
  REQUIRE(firstProgress);
  CHECK(*firstProgress > kept);

  const auto m = s.manifest(id);
  REQUIRE(m);
  const Probe p = probe(s.store.mediaPath(*m));
  MESSAGE("resumed after " << keptSegments << " segments; duration " << p.duration << " s");
  const double runTime = item.value(QStringLiteral("RunTimeTicks")).toDouble() / TicksPerSecond;
  CHECK(std::abs(p.duration - runTime) <= 1.0);
  CHECK(p.seekMs < 2000);
}

TEST_CASE("Test 15: a music track compressed to Opus 128 kbit/s keeps its length") {
  if (!hasExtrasStand()) return;
  Stand s;
  const QJsonObject item = s.find(QStringLiteral("Audio"), QStringLiteral("Tone 1"));
  const QString id = item.value(QStringLiteral("Id")).toString();
  REQUIRE(s.queue->enqueueCompressed(item, firstSource(item), QStringLiteral("audio-opus128")) ==
          EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !s.completed.isEmpty() || !s.failures.isEmpty(); }, 120000));
  REQUIRE(s.failures.isEmpty());
  const auto m = s.manifest(id);
  REQUIRE(m);
  CHECK(m->transcode.audioCodec == QStringLiteral("opus"));
  const Probe p = probe(s.store.mediaPath(*m));
  const double runTime = item.value(QStringLiteral("RunTimeTicks")).toDouble() / TicksPerSecond;
  CHECK(std::abs(p.duration - runTime) <= 1.0);
  CHECK(p.seekMs < 2000);
  // 240 s at about 128 kbit/s (VBR): well under the FLAC original.
  CHECK(m->expectedSize < 8'000'000);
}
