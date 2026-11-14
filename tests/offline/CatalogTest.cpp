#include "../support/DoctestQt.h"
#include "OfflineFixtures.h"

#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/Manifest.h>
#include <jfd/offline/OfflineCatalog.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace jfd::offline;
using namespace jfd::test;

namespace {
// The threshold "Downloads: catalog" is defined for CI Linux, which runs
// this test in Release (api.yml). AddressSanitizer slows file reading and
// JSON parsing about 2.5 times, and Windows runners scan every file they
// open, so there the time is reported and only checked against a bound
// that catches a broken (quadratic) index.
#if defined(__SANITIZE_ADDRESS__)
  constexpr bool Instrumented = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
  constexpr bool Instrumented = true;
#else
  constexpr bool Instrumented = false;
#endif
#else
  constexpr bool Instrumented = false;
#endif
#if defined(Q_OS_LINUX)
  constexpr bool ThresholdApplies = !Instrumented;
#else
  constexpr bool ThresholdApplies = false;
#endif

  // A track as the server describes it: about 1.5 KB of JSON; a movie with
  // its media source about 3 KB.
  QJsonObject trackJson(const QString& id, int n) {
    QJsonArray artists;
    artists.append(QJsonObject{{QStringLiteral("Name"), QStringLiteral("Bob and the Fixtures")},
                               {QStringLiteral("Id"), itemIdFor(900000 + n % 50)}});
    return QJsonObject{
        {QStringLiteral("Id"), id},
        {QStringLiteral("Name"), QStringLiteral("Track number %1 of the test album").arg(n)},
        {QStringLiteral("ServerId"), ServerId},
        {QStringLiteral("Type"), QStringLiteral("Audio")},
        {QStringLiteral("MediaType"), QStringLiteral("Audio")},
        {QStringLiteral("Album"), QStringLiteral("Test album %1").arg(n / 12)},
        {QStringLiteral("AlbumId"), itemIdFor(800000 + n / 12)},
        {QStringLiteral("AlbumArtist"), QStringLiteral("Bob and the Fixtures")},
        {QStringLiteral("ArtistItems"), artists},
        {QStringLiteral("AlbumArtists"), artists},
        {QStringLiteral("IndexNumber"), n % 12 + 1},
        {QStringLiteral("ParentIndexNumber"), 1},
        {QStringLiteral("RunTimeTicks"), 2400000000.0},
        {QStringLiteral("ProductionYear"), 2020},
        {QStringLiteral("ImageTags"),
         QJsonObject{
             {QStringLiteral("Primary"), QStringLiteral("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f")}}},
        {QStringLiteral("AlbumPrimaryImageTag"),
         QStringLiteral("1e1e1e1e1e1e1e1e1e1e1e1e1e1e1e1e")},
        {QStringLiteral("Genres"), QJsonArray{QStringLiteral("Rock"), QStringLiteral("Test")}},
    };
  }

  void writeManifest(const DownloadStore& store, ItemManifest m) {
    QDir().mkpath(store.itemDir(m));
    QFile file(store.manifestPath(m));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(toJson(m)).toJson(QJsonDocument::Compact));
  }
} // namespace

TEST_CASE("10,000 manifests (200 videos, 9,800 tracks) load in under a second") {
  QTemporaryDir dir;
  DownloadStore store(dir.filePath(QStringLiteral("downloads")));
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (int n = 0; n < 10000; ++n) {
    ItemManifest m;
    m.serverId = ServerId;
    m.userId = UserId;
    m.itemId = itemIdFor(n);
    m.state = ItemState::Complete;
    if (n < 200) {
      m.mediaSource = mediaSourceJson(m.itemId, 4 * GiB);
      m.item = itemJson(m.itemId, m.mediaSource);
      m.mediaFile = QStringLiteral("media.mkv");
      m.expectedSize = 4 * GiB;
    } else {
      m.item = trackJson(m.itemId, n);
      m.mediaSource = QJsonObject{{QStringLiteral("Id"), m.itemId},
                                  {QStringLiteral("Container"), QStringLiteral("flac")},
                                  {QStringLiteral("Size"), 30000000},
                                  {QStringLiteral("Protocol"), QStringLiteral("File")}};
      m.mediaFile = QStringLiteral("media.flac");
      m.expectedSize = 30000000;
    }
    m.userData = QJsonObject{{QStringLiteral("PlayCount"), n % 3},
                             {QStringLiteral("Played"), n % 2 == 0},
                             {QStringLiteral("PlaybackPositionTicks"), 0}};
    m.queuedAt = now;
    m.completedAt = now;
    writeManifest(store, m);
  }

  OfflineCatalog catalog(store);
  QElapsedTimer clock;
  clock.start();
  const qsizetype loaded = catalog.load();
  const qint64 elapsed = clock.elapsed();
  MESSAGE("catalog load of 10,000 manifests: " << elapsed << " ms");
  CHECK(loaded == 10000);
  // Threshold "Downloads: catalog".
  constexpr qint64 Limit = ThresholdApplies ? 1000 : 5000;
  CHECK(elapsed < Limit);
  CHECK(catalog.items(ServerId, UserId).size() == 10000);
  CHECK(catalog.items(ServerId, QStringLiteral("b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0")).isEmpty());
  CHECK(catalog.estimatedBytes() == 200 * 4 * GiB + std::int64_t{9800} * 30000000);
  CHECK(catalog.find(ServerId, UserId, itemIdFor(5000))
            ->item.value(QStringLiteral("Type"))
            .toString() == QStringLiteral("Audio"));
}

TEST_CASE("A damaged or misplaced manifest is skipped, the rest still load") {
  QTemporaryDir dir;
  DownloadStore store(dir.filePath(QStringLiteral("downloads")));
  ItemManifest good;
  good.serverId = ServerId;
  good.userId = UserId;
  good.itemId = itemIdFor(1);
  REQUIRE(store.writeManifest(good));

  ItemManifest torn = good;
  torn.itemId = itemIdFor(2);
  REQUIRE(store.writeManifest(torn));
  QFile file(store.manifestPath(torn));
  REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write("{\"format\":1,\"serverId\":");
  file.close();

  // A copy of item 1's manifest in item 3's folder is not item 3.
  QDir().mkpath(store.itemDir(ServerId, UserId, itemIdFor(3)));
  QFile::copy(store.manifestPath(good),
              store.itemDir(ServerId, UserId, itemIdFor(3)) + QStringLiteral("/item.json"));

  OfflineCatalog catalog(store);
  CHECK(catalog.load() == 1);
  CHECK(catalog.find(ServerId, UserId, itemIdFor(1)));
}
