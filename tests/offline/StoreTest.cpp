#include "../support/DoctestQt.h"
#include "OfflineFixtures.h"

#include <jfd/api/Downloads.h>
#include <jfd/api/Models.h>
#include <jfd/offline/DownloadSettings.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/Manifest.h>
#include <jfd/settings/Settings.h>

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace jfd::offline;
using namespace jfd::test;

TEST_CASE("The manifest keeps the server's JSON but no token, TranscodingUrl or DirectStreamUrl") {
  const QString id = itemIdFor(1);
  ItemManifest m;
  m.serverId = ServerId;
  m.userId = UserId;
  m.itemId = id;
  m.mediaSource = mediaSourceJson(id, 1234);
  m.item = itemJson(id, m.mediaSource);
  m.userData = QJsonObject{
      {QStringLiteral("PlaybackPositionTicks"), 0},
      {QStringLiteral("Key"), QStringLiteral("x?api_key=") + QString::fromLatin1(Token)}};
  m.mediaFile = QStringLiteral("media.mkv");
  m.expectedSize = 1234;
  m.queuedAt = QDateTime::currentDateTimeUtc();

  const QJsonObject stored = toJson(m);
  const QByteArray text = QJsonDocument(stored).toJson();
  CHECK_FALSE(text.contains(Token));
  CHECK_FALSE(text.toLower().contains("apikey"));
  CHECK_FALSE(text.toLower().contains("api_key"));
  CHECK_FALSE(text.contains("TranscodingUrl"));
  CHECK_FALSE(text.contains("DirectStreamUrl"));

  const auto back = manifestFromJson(stored);
  REQUIRE(back);
  CHECK(back->itemId == id);
  CHECK(back->expectedSize == 1234);
  CHECK(back->state == ItemState::Queued);
  // Everything else is the server's JSON as it was, parsed by api::json.
  const auto source = jfd::api::json::mediaSource(back->mediaSource);
  REQUIRE(source);
  CHECK(source->size == 1234);
  CHECK(source->bitrate == 8000000);
  CHECK(source->transcodingUrl.isEmpty());
  CHECK(back->item.value(QStringLiteral("Overview")).toString() ==
        QStringLiteral("Bob asks: is a=b? Nobody knows."));
  // Other query parameters survive; only the key goes.
  const QString delivery = back->mediaSource.value(QStringLiteral("MediaStreams"))
                               .toArray()
                               .at(0)
                               .toObject()
                               .value(QStringLiteral("DeliveryUrl"))
                               .toString();
  CHECK(delivery == QStringLiteral("/Videos/") + id + QStringLiteral("/Subtitles/2/0/Stream.srt"));
  CHECK(scrubSecrets(QJsonObject{{QStringLiteral("u"), QStringLiteral("/a?b=1&ApiKey=s&c=2")}})
            .value(QStringLiteral("u"))
            .toString() == QStringLiteral("/a?b=1&c=2"));
  CHECK(scrubSecrets(QJsonObject{{QStringLiteral("u"), QStringLiteral("/a?api_key=s&c=2")}})
            .value(QStringLiteral("u"))
            .toString() == QStringLiteral("/a?c=2"));
}

TEST_CASE("Manifests of an unknown format or without ids are not read") {
  CHECK_FALSE(manifestFromJson(QJsonObject{}));
  CHECK_FALSE(manifestFromJson(QJsonObject{{QStringLiteral("format"), 2},
                                           {QStringLiteral("serverId"), ServerId},
                                           {QStringLiteral("userId"), UserId},
                                           {QStringLiteral("itemId"), itemIdFor(1)},
                                           {QStringLiteral("state"), QStringLiteral("queued")}}));
  CHECK_FALSE(manifestFromJson(QJsonObject{{QStringLiteral("format"), 1},
                                           {QStringLiteral("serverId"), ServerId},
                                           {QStringLiteral("userId"), UserId},
                                           {QStringLiteral("itemId"), itemIdFor(1)},
                                           {QStringLiteral("state"), QStringLiteral("whatever")}}));
}

TEST_CASE("Ids that could leave the download folder are refused") {
  CHECK(DownloadStore::isSafeId(ServerId));
  CHECK(DownloadStore::isSafeId(QStringLiteral("2b5c1e7a-0d4e-4a43-9a51-0f5d1c2b3a4d")));
  CHECK_FALSE(DownloadStore::isSafeId(QStringLiteral("..")));
  CHECK_FALSE(DownloadStore::isSafeId(QStringLiteral("a/../b")));
  CHECK_FALSE(DownloadStore::isSafeId(QStringLiteral("a\\b")));
  CHECK_FALSE(DownloadStore::isSafeId(QStringLiteral("C:")));
  CHECK_FALSE(DownloadStore::isSafeId(QString()));

  QTemporaryDir dir;
  DownloadStore store(dir.path());
  ItemManifest m;
  m.serverId = ServerId;
  m.userId = UserId;
  m.itemId = QStringLiteral("../escape");
  CHECK_FALSE(store.writeManifest(m));
  CHECK_FALSE(QFile::exists(dir.filePath(QStringLiteral("escape"))));
}

TEST_CASE("Media file names come from the container, never from the server's path as is") {
  CHECK(mediaExtension(QJsonObject{{QStringLiteral("Container"), QStringLiteral("mkv,webm")}}) ==
        QStringLiteral("mkv"));
  CHECK(mediaExtension(QJsonObject{{QStringLiteral("Path"), QStringLiteral("/m/a.FLAC")}}) ==
        QStringLiteral("flac"));
  CHECK(mediaExtension(QJsonObject{{QStringLiteral("Container"), QStringLiteral("../x")}}) ==
        QStringLiteral("bin"));
}

namespace {
  void roundTripIn(const QString& root) {
    DownloadStore store(root);
    ItemManifest m;
    m.serverId = ServerId;
    m.userId = UserId;
    m.itemId = itemIdFor(7);
    m.mediaSource = mediaSourceJson(m.itemId, 10);
    m.item = itemJson(m.itemId, m.mediaSource);
    m.mediaFile = QStringLiteral("media.mkv");
    m.queuedAt = QDateTime::currentDateTimeUtc();
    REQUIRE(store.writeManifest(m));
    QFile part(store.partPath(m));
    REQUIRE(part.open(QIODevice::WriteOnly));
    part.write("0123456789");
    part.close();
    REQUIRE(QFile::rename(store.partPath(m), store.mediaPath(m)));
    m.state = ItemState::Complete;
    REQUIRE(store.writeManifest(m));

    const auto back = store.readManifest(ServerId, UserId, m.itemId);
    REQUIRE(back);
    CHECK(back->state == ItemState::Complete);
    QFile media(store.mediaPath(*back));
    REQUIRE(media.open(QIODevice::ReadOnly));
    CHECK(media.readAll() == QByteArray("0123456789"));
    media.close();
    CHECK(store.scan().size() == 1);
    CHECK(store.removeItem(*back));
    CHECK(store.scan().isEmpty());
  }
} // namespace

TEST_CASE("A download folder with Cyrillic letters and spaces works") {
  QTemporaryDir dir;
  roundTripIn(dir.filePath(QStringLiteral("Мои загрузки/папка с пробелами")));
}

TEST_CASE("A download folder deeper than Windows' 260 characters works") {
  QTemporaryDir dir;
  QString root = dir.path();
  // Folder names of 40 characters until the media path is past 300.
  while (root.size() < 230)
    root += QStringLiteral("/deep folder level with a long name 0123");
  roundTripIn(root);
  const QString media = root + QLatin1Char('/') + ServerId + QLatin1Char('/') + UserId +
                        QLatin1Char('/') + itemIdFor(7) + QStringLiteral("/media.mkv");
  CHECK(media.size() > 300);
}

TEST_CASE("mpv gets long Windows paths with the \\\\?\\ prefix") {
  const QString shortPath = QStringLiteral("C:/Users/alice/Videos/Jellyfin/a/b/media.mkv");
  CHECK(DownloadStore::windowsExternalPath(shortPath) ==
        QStringLiteral("C:\\Users\\alice\\Videos\\Jellyfin\\a\\b\\media.mkv"));
  const QString longPath =
      QStringLiteral("C:/") + QString(300, QLatin1Char('d')) + QStringLiteral("/media.mkv");
  CHECK(DownloadStore::windowsExternalPath(longPath).startsWith(QStringLiteral("\\\\?\\C:\\ddd")));
  const QString share = QStringLiteral("//nas/share/") + QString(300, QLatin1Char('d'));
  CHECK(DownloadStore::windowsExternalPath(share).startsWith(
      QStringLiteral("\\\\?\\UNC\\nas\\share\\")));
#ifndef Q_OS_WIN
  CHECK(DownloadStore::externalPath(longPath) == longPath);
#endif
}

TEST_CASE("Downloads go to Videos/Jellyfin with 100 GiB and 5 GiB free by default") {
  jfd::settings::SettingsRegistry registry;
  REQUIRE(registerDownloadSettings(registry));
  CHECK_FALSE(registerDownloadSettings(registry));
  QTemporaryDir dir;
  jfd::settings::Settings settings(registry, dir.filePath(QStringLiteral("settings.ini")));
  CHECK(settings.value(keys::MaxGiB) == QVariant(100));
  CHECK(settings.value(keys::ReserveGiB) == QVariant(5));
  const QString folder = settings.value(keys::Folder).toString();
  CHECK(QDir::fromNativeSeparators(folder) == defaultDownloadFolder());
  CHECK(defaultDownloadFolder().endsWith(QStringLiteral("/Jellyfin")));
  CHECK(settings.setValue(keys::MaxGiB, 0));
  CHECK_FALSE(settings.setValue(keys::MaxGiB, -1));
}

TEST_CASE("The original is the default download when the user may download files") {
  jfd::api::UserInfo user;
  CHECK(jfd::api::defaultDownloadQuality(user, true) == jfd::api::DownloadQuality::None);
  user.enableVideoPlaybackTranscoding = true;
  CHECK(jfd::api::defaultDownloadQuality(user, true) == jfd::api::DownloadQuality::Compressed);
  CHECK(jfd::api::defaultDownloadQuality(user, false) == jfd::api::DownloadQuality::None);
  user.enableContentDownloading = true;
  CHECK(jfd::api::defaultDownloadQuality(user, true) == jfd::api::DownloadQuality::Original);

  const auto parsed = jfd::api::json::user(
      QJsonObject{{QStringLiteral("Id"), UserId},
                  {QStringLiteral("Policy"),
                   QJsonObject{{QStringLiteral("EnableContentDownloading"), true},
                               {QStringLiteral("EnableAudioPlaybackTranscoding"), true}}}});
  REQUIRE(parsed);
  CHECK(parsed->enableContentDownloading);
  CHECK(parsed->enableAudioPlaybackTranscoding);
  CHECK_FALSE(parsed->enableVideoPlaybackTranscoding);
}

TEST_CASE("Folders, disc images, remote and .strm sources have no original to download") {
  const QJsonObject source = mediaSourceJson(itemIdFor(1), 10);
  CHECK(jfd::api::originalAvailable(itemJson(itemIdFor(1), source), source));
  QJsonObject iso = itemJson(itemIdFor(1), source);
  iso.insert(QStringLiteral("VideoType"), QStringLiteral("Iso"));
  CHECK_FALSE(jfd::api::originalAvailable(iso, source));
  QJsonObject remote = source;
  remote.insert(QStringLiteral("IsRemote"), true);
  CHECK_FALSE(jfd::api::originalAvailable(itemJson(itemIdFor(1), remote), remote));
  QJsonObject http = source;
  http.insert(QStringLiteral("Protocol"), QStringLiteral("Http"));
  CHECK_FALSE(jfd::api::originalAvailable(itemJson(itemIdFor(1), http), http));
  QJsonObject strm = source;
  strm.insert(QStringLiteral("Container"), QStringLiteral("strm"));
  CHECK_FALSE(jfd::api::originalAvailable(itemJson(itemIdFor(1), strm), strm));
}

TEST_CASE("The token goes only to the server's own scheme, host and port") {
  const QUrl server(QStringLiteral("https://media.example.org/jellyfin"));
  CHECK(jfd::api::isServerUrl(
      server, QUrl(QStringLiteral("https://media.example.org:443/jellyfin/Items/1/Download"))));
  CHECK_FALSE(jfd::api::isServerUrl(
      server, QUrl(QStringLiteral("http://media.example.org/jellyfin/Items/1/Download"))));
  CHECK_FALSE(jfd::api::isServerUrl(
      server, QUrl(QStringLiteral("https://media.example.org:8920/Items/1/Download"))));
  CHECK_FALSE(jfd::api::isServerUrl(
      server, QUrl(QStringLiteral("https://cdn.example.org/Items/1/Download"))));
  CHECK_FALSE(jfd::api::isServerUrl(
      server, QUrl(QStringLiteral("https://bob:x@media.example.org/Items/1/Download"))));
  CHECK(jfd::api::originalDownloadUrl(server, itemIdFor(3)) ==
        QUrl(QStringLiteral("https://media.example.org/jellyfin/Items/") + itemIdFor(3) +
             QStringLiteral("/Download")));
}

TEST_CASE("The guard keeps the limit and the reserve, with a 10 % margin on estimates") {
  FakeStorageGuard guard(100 * GiB, 5 * GiB);
  guard.free = 20 * GiB;
  CHECK(guard.admit(QStringLiteral("/x"), 10 * GiB, 50 * GiB) == StorageVerdict::Ok);
  // 14 GiB + 10 % leaves less than 5 GiB.
  CHECK(guard.admit(QStringLiteral("/x"), 14 * GiB, 50 * GiB) == StorageVerdict::LowSpace);
  CHECK(guard.admit(QStringLiteral("/x"), 1 * GiB, 101 * GiB) == StorageVerdict::OverLimit);
  guard.setLimitBytes(0);
  CHECK(guard.admit(QStringLiteral("/x"), 1 * GiB, 1000 * GiB) == StorageVerdict::Ok);
  guard.free = 4 * GiB;
  CHECK(guard.check(QStringLiteral("/x")) == StorageVerdict::LowSpace);
  // A drive that is not there.
  guard.free = -1;
  CHECK(guard.check(QStringLiteral("/x")) == StorageVerdict::LowSpace);

  // The real query answers for a folder that does not exist yet.
  StorageGuard real;
  QTemporaryDir dir;
  CHECK(real.freeBytes(dir.filePath(QStringLiteral("not/yet/there"))).value_or(0) > 0);
}
