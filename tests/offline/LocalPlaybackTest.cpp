#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "OfflineFixtures.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/ExtrasDownloader.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/OfflineLocalSource.h>
#include <jfd/playback/StreamPlan.h>
#include <jfd/playback/TrackTable.h>
#include <jfd/session/PlaybackLauncher.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <variant>

using namespace jfd;
using namespace jfd::offline;
using namespace jfd::test;

namespace {

  api::ClientIdentity identity() {
    return api::ClientIdentity{QStringLiteral("Test Client"), QStringLiteral("1.0"),
                               QStringLiteral("Test Device"), QStringLiteral("device1")};
  }

  bool writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  }

  // A source with an external subtitle (0), video (1), audio (2) and an
  // embedded ASS subtitle (3) that PlaybackInfo would deliver extracted.
  QJsonObject videoSource(const QString& id) {
    QJsonObject source = mediaSourceJson(id, 4096);
    source.insert(QStringLiteral("RunTimeTicks"), 72000000000.0);
    source.insert(
        QStringLiteral("MediaStreams"),
        QJsonArray{
            QJsonObject{{QStringLiteral("Index"), 0},
                        {QStringLiteral("Type"), QStringLiteral("Subtitle")},
                        {QStringLiteral("Codec"), QStringLiteral("subrip")},
                        {QStringLiteral("Language"), QStringLiteral("rus")},
                        {QStringLiteral("IsExternal"), true},
                        {QStringLiteral("DeliveryMethod"), QStringLiteral("External")}},
            QJsonObject{{QStringLiteral("Index"), 1},
                        {QStringLiteral("Type"), QStringLiteral("Video")},
                        {QStringLiteral("Codec"), QStringLiteral("h264")}},
            QJsonObject{{QStringLiteral("Index"), 2},
                        {QStringLiteral("Type"), QStringLiteral("Audio")},
                        {QStringLiteral("Codec"), QStringLiteral("aac")},
                        {QStringLiteral("Language"), QStringLiteral("jpn")}},
            QJsonObject{{QStringLiteral("Index"), 3},
                        {QStringLiteral("Type"), QStringLiteral("Subtitle")},
                        {QStringLiteral("Codec"), QStringLiteral("ass")},
                        {QStringLiteral("Language"), QStringLiteral("eng")},
                        {QStringLiteral("DeliveryMethod"), QStringLiteral("External")},
                        {QStringLiteral("DeliveryUrl"), QStringLiteral("/Videos/x/Subtitles/3/0/Stream.ass")}},
        });
    return source;
  }

  const char* SegmentsJson = R"({"Items":[
    {"Id":"s1","ItemId":"x","Type":"Intro","StartTicks":20000000,"EndTicks":120000000},
    {"Id":"s2","ItemId":"x","Type":"Outro","StartTicks":500000000,"EndTicks":580000000}]})";

  // A complete download in a folder whose path has Cyrillic letters and
  // spaces.
  struct Downloaded {
    QTemporaryDir dir;
    DownloadStore store{dir.filePath(QStringLiteral("Загрузки alice/Jellyfin files"))};
    OfflineCatalog catalog{store};
    OfflineLocalSource local{store, catalog};
    QString itemId = itemIdFor(7);

    explicit Downloaded(const QString& quality = QStringLiteral("original")) {
      const QJsonObject source = videoSource(itemId);
      ItemManifest m;
      m.serverId = ServerId;
      m.userId = UserId;
      m.itemId = itemId;
      m.state = ItemState::Complete;
      m.quality = quality;
      m.item = itemJson(itemId, source);
      m.mediaSource = source;
      m.mediaFile = QStringLiteral("media.mkv");
      m.expectedSize = 4096;
      m.subtitleFiles.insert(0, QStringLiteral("subs/0.srt"));
      m.segmentsFile = QStringLiteral("segments.json");
      m.extrasComplete = true;
      REQUIRE(store.writeManifest(m));
      const QString folder = store.itemDir(m);
      REQUIRE(writeFile(folder + QStringLiteral("/media.mkv"), QByteArray(4096, 'm')));
      REQUIRE(writeFile(folder + QStringLiteral("/subs/0.srt"), "1\n00:00:01,000 --> 00:00:02,000\nHi\n"));
      REQUIRE(writeFile(folder + QStringLiteral("/segments.json"), SegmentsJson));
      catalog.load();
      local.setUser(ServerId, UserId);
    }

    QString mediaPath() const { return store.mediaPath(*catalog.find(ServerId, UserId, itemId)); }
  };

  // The launcher on a server that answers nothing useful, recording what it
  // posts and resolves.
  struct Launch {
    FakeServer server{[](const HttpRequest&) { return HttpResponse{500, "{}"}; }};
    api::JellyfinClient client{identity()};
    std::vector<core::SessionInput> posted;
    std::unique_ptr<session::PlaybackLauncher> launcher;
    std::optional<api::PlaybackStream> stream;
    std::optional<api::Item> item;
    QStringList failures;

    explicit Launch(const session::LocalSource* local) {
      client.setServerUrl(server.url());
      client.setSession(QStringLiteral("tok3n"), UserId);
      launcher = std::make_unique<session::PlaybackLauncher>(
          client, QJsonObject{}, [this](core::SessionInput input) { posted.push_back(std::move(input)); });
      launcher->setLocalSource(local);
      QObject::connect(launcher.get(), &session::PlaybackLauncher::resolved,
                       [this](const session::PlaybackLauncher::Request&, const api::Item& i,
                              const api::MediaSource&, const api::PlaybackStream& s) {
                         item = i;
                         stream = s;
                       });
      QObject::connect(launcher.get(), &session::PlaybackLauncher::failed,
                       [this](const QString&, const QString& message) { failures.append(message); });
    }
  };

} // namespace

TEST_CASE("A downloaded copy opens without the server: file path, local subtitles, segments") {
  Downloaded d;
  d.local.setOnline(false);
  Launch l(&d.local);

  session::PlaybackLauncher::Request request;
  request.itemId = d.itemId;
  request.startTicks = 600000000;
  l.launcher->open(request);
  REQUIRE(waitFor([&] { return !l.posted.empty(); }));

  // Not one request to the server.
  CHECK(l.server.requests().isEmpty());
  REQUIRE(std::holds_alternative<core::intent::Open>(l.posted.front()));
  const core::MediaRequest& media = std::get<core::intent::Open>(l.posted.front()).media;
  // A file system path, never a percent-encoded file:// URL.
  const std::string expected = playback::playerAddress(QUrl::fromLocalFile(d.mediaPath()));
  CHECK(media.url == expected);
  CHECK(QString::fromStdString(media.url).indexOf(QLatin1String("file:")) < 0);
  CHECK(QString::fromStdString(media.url).contains(QStringLiteral("Загрузки alice")));
  CHECK(media.start.ticks() == 600000000);
  REQUIRE(media.runTime.has_value());
  CHECK(media.runTime->ticks() == 72000000000);
  CHECK(media.title == "Alice in the Test");

  REQUIRE(l.stream.has_value());
  CHECK(l.stream->method == api::PlayMethod::DirectPlay);
  CHECK(l.stream->playSessionId.isEmpty());
  REQUIRE(l.stream->externalSubtitles.size() == 1);
  CHECK(l.stream->externalSubtitles.front().streamIndex == 0);
  CHECK(l.stream->externalSubtitles.front().url.isLocalFile());

  // The track menu: the external file is added by path; the embedded ASS
  // (which PlaybackInfo had marked for extraction) is a track of the file.
  const auto copy = d.local.find(d.itemId);
  REQUIRE(copy.has_value());
  CHECK(copy->segments.size() == 2);
  CHECK(copy->original);
  playback::TrackTable table(copy->source, *l.stream);
  CHECK(table.tracks(core::TrackKind::Subtitle).size() == 2);
  bool addedByPath = false;
  for (const playback::Command& command : table.selectInitial(2, 0))
    if (const auto* add = std::get_if<playback::command::Add>(&command))
      addedByPath = QString::fromStdString(add->track.url).endsWith(QStringLiteral("0.srt")) &&
                    add->track.url.find("file:") == std::string::npos;
  CHECK(addedByPath);
  bool embedded = false;
  for (const playback::Command& command : table.select(core::TrackKind::Subtitle, 3))
    if (const auto* select = std::get_if<playback::command::Select>(&command))
      embedded = select->trackId == 1;
  CHECK(embedded);

  // Preloading the next item (music) uses the copy too.
  l.posted.clear();
  l.launcher->preloadNext(request);
  REQUIRE(waitFor([&] { return !l.posted.empty(); }));
  CHECK(std::holds_alternative<core::intent::PreloadNext>(l.posted.front()));
  CHECK(l.server.requests().isEmpty());
}

TEST_CASE("With the server reachable, the copy plays only while Play downloads from disk is on") {
  Downloaded d;
  d.local.setOnline(true);
  d.local.setPreferLocal(false);
  Launch l(&d.local);
  session::PlaybackLauncher::Request request;
  request.itemId = d.itemId;
  l.launcher->open(request);
  // The usual way: the server is asked (and fails here).
  REQUIRE(waitFor([&] { return !l.failures.isEmpty(); }));
  CHECK_FALSE(l.server.requests().isEmpty());
  CHECK(l.posted.empty());

  d.local.setPreferLocal(true);
  const qsizetype asked = l.server.requests().size();
  l.launcher->open(request);
  REQUIRE(waitFor([&] { return !l.posted.empty(); }));
  CHECK(l.server.requests().size() == asked);
}

TEST_CASE("Another version, a compressed copy in a group, or no copy go to the server") {
  SUBCASE("another version of the item") {
    Downloaded d;
    d.local.setOnline(false);
    Launch l(&d.local);
    session::PlaybackLauncher::Request request;
    request.itemId = d.itemId;
    request.mediaSourceId = itemIdFor(99);
    l.launcher->open(request);
    REQUIRE(waitFor([&] { return !l.failures.isEmpty(); }));
    CHECK_FALSE(l.server.requests().isEmpty());
  }
  SUBCASE("a compressed copy in a group") {
    Downloaded d(QStringLiteral("compressed"));
    d.local.setOnline(false);
    Launch l(&d.local);
    session::PlaybackLauncher::Request request;
    request.itemId = d.itemId;
    request.playlistItem = core::PlaylistItemId(std::string("p1"));
    l.launcher->open(request);
    REQUIRE(waitFor([&] { return !l.failures.isEmpty(); }));
    CHECK_FALSE(l.server.requests().isEmpty());
    // Outside the group it plays.
    request.playlistItem = {};
    l.launcher->open(request);
    REQUIRE(waitFor([&] { return !l.posted.empty(); }));
  }
  SUBCASE("an original copy in a group plays from disk") {
    Downloaded d;
    d.local.setOnline(true);
    Launch l(&d.local);
    session::PlaybackLauncher::Request request;
    request.itemId = d.itemId;
    request.playlistItem = core::PlaylistItemId(std::string("p1"));
    l.launcher->open(request);
    REQUIRE(waitFor([&] { return !l.posted.empty(); }));
    CHECK(l.server.requests().isEmpty());
    CHECK(std::get<core::intent::Open>(l.posted.front()).media.playlistItem == request.playlistItem);
  }
  SUBCASE("a copy whose file is missing") {
    Downloaded d;
    d.local.setOnline(false);
    REQUIRE(QFile::remove(d.mediaPath()));
    CHECK_FALSE(d.local.find(d.itemId).has_value());
  }
  SUBCASE("another user's copy") {
    Downloaded d;
    d.local.setOnline(false);
    d.local.setUser(ServerId, QStringLiteral("b0bb0bb0bb0bb0bb0bb0bb0bb0bb0b02"));
    CHECK_FALSE(d.local.find(d.itemId).has_value());
    d.local.setUser({}, {});
    CHECK_FALSE(d.local.find(d.itemId).has_value());
  }
}

TEST_CASE("mpv gets file system paths; long Windows paths get the \\\\?\\ prefix") {
  CHECK(playback::playerAddress(QUrl(QStringLiteral("http://h/Videos/1/stream?static=true"))) ==
        "http://h/Videos/1/stream?static=true");
#ifndef Q_OS_WIN
  CHECK(playback::playerAddress(QUrl::fromLocalFile(QStringLiteral("/tmp/a b/Загрузки/media.mkv"))) ==
        QStringLiteral("/tmp/a b/Загрузки/media.mkv").toStdString());
#endif
  const QString deep = QStringLiteral("C:/Users/alice/Videos/Jellyfin/") + QString(240, QLatin1Char('d')) +
                       QStringLiteral("/media.mkv");
  CHECK(playback::windowsPlayerPath(deep).startsWith(QStringLiteral("\\\\?\\C:\\Users\\alice")));
  CHECK(playback::windowsPlayerPath(QStringLiteral("C:/short/media.mkv")) ==
        QStringLiteral("C:\\short\\media.mkv"));
  CHECK(playback::windowsPlayerPath(QStringLiteral("//nas/share/") + QString(260, QLatin1Char('x')))
            .startsWith(QStringLiteral("\\\\?\\UNC\\nas\\share\\")));
  // The download folder uses the same rule.
  CHECK(DownloadStore::windowsExternalPath(deep) == playback::windowsPlayerPath(deep));
}

TEST_CASE("Lyrics of a downloaded track come from its file") {
  QTemporaryDir dir;
  DownloadStore store(dir.filePath(QStringLiteral("downloads")));
  OfflineCatalog catalog(store);
  OfflineLocalSource local(store, catalog);
  local.setUser(ServerId, UserId);
  const QString track = itemIdFor(30);
  const QJsonObject source = mediaSourceJson(track, 100, QStringLiteral("flac"));
  ItemManifest m;
  m.serverId = ServerId;
  m.userId = UserId;
  m.itemId = track;
  m.state = ItemState::Complete;
  m.item = itemJson(track, source, QStringLiteral("Audio"));
  m.mediaSource = source;
  m.mediaFile = QStringLiteral("media.flac");
  m.extrasComplete = true;
  REQUIRE(store.writeManifest(m));
  catalog.load();
  // Fetched and none on the server: empty, not "ask the server".
  REQUIRE(local.lyrics(track).has_value());
  CHECK(local.lyrics(track)->isEmpty());
  CHECK_FALSE(local.lyrics(itemIdFor(31)).has_value());

  m.lyricsFile = QStringLiteral("lyrics.json");
  REQUIRE(store.writeManifest(m));
  REQUIRE(writeFile(store.itemDir(m) + QStringLiteral("/lyrics.json"),
                    R"({"Lyrics":[{"Text":"one","Start":0},{"Text":"two","Start":20000000}]})"));
  catalog.load();
  const auto lyrics = local.lyrics(track);
  REQUIRE(lyrics.has_value());
  CHECK(lyrics->lines.size() == 2);
  CHECK(lyrics->synced);
}

TEST_CASE("A finished download gets its subtitles, images, lyrics and segments from its own server") {
  QHash<QString, int> asked;
  QList<QByteArray> authorizations;
  FakeServer server([&](const HttpRequest& r) {
    asked[r.path] += 1;
    authorizations.append(r.headers.value("authorization"));
    if (r.path.endsWith(QLatin1String("/Subtitles/0/Stream.srt")))
      return HttpResponse{200, "1\n00:00:01,000 --> 00:00:02,000\nHi\n", "text/plain"};
    if (r.path.contains(QLatin1String("/Images/Primary")))
      return HttpResponse{200, QByteArray(300, 'p'), "image/jpeg"};
    if (r.path.contains(QLatin1String("/Images/Backdrop/0")))
      return HttpResponse{200, QByteArray(500, 'b'), "image/jpeg"};
    if (r.path.startsWith(QLatin1String("/MediaSegments/"))) return HttpResponse{200, SegmentsJson};
    return HttpResponse{404, {}};
  });

  Downloaded d;
  // Nothing fetched yet.
  auto m = *d.catalog.find(ServerId, UserId, d.itemId);
  m.subtitleFiles.clear();
  m.segmentsFile.clear();
  m.extrasComplete = false;
  m.item.insert(QStringLiteral("ImageTags"), QJsonObject{{QStringLiteral("Primary"), QStringLiteral("t1")}});
  m.item.insert(QStringLiteral("BackdropImageTags"), QJsonArray{QStringLiteral("t2")});
  REQUIRE(d.store.writeManifest(m));
  d.catalog.update(m);
  QFile::remove(d.store.itemDir(m) + QStringLiteral("/subs/0.srt"));

  ExtrasDownloader extras(d.store, d.catalog);
  QStringList done;
  QObject::connect(&extras, &ExtrasDownloader::finished,
                   [&](const QString& id, bool complete) { done.append(id + QLatin1String(complete ? "+" : "-")); });
  extras.setAccess(ServerId, UserId, server.url(), "MediaBrowser Token=\"" + Token + "\"");
  extras.fetchMissing();
  REQUIRE(waitFor([&] { return !done.isEmpty(); }));
  CHECK(done == QStringList{d.itemId + QStringLiteral("+")});

  const auto after = d.catalog.find(ServerId, UserId, d.itemId);
  REQUIRE(after.has_value());
  CHECK(after->extrasComplete);
  CHECK(after->subtitleFiles.value(0) == QStringLiteral("subs/0.srt"));
  CHECK(after->imageFiles.value(QStringLiteral("Primary")) == QStringLiteral("images/Primary"));
  CHECK(after->imageFiles.value(QStringLiteral("Backdrop")) == QStringLiteral("images/Backdrop"));
  CHECK(after->segmentsFile == QStringLiteral("segments.json"));
  CHECK(QFileInfo(d.store.itemDir(*after) + QStringLiteral("/images/Primary")).size() == 300);
  // Read back from disk, as at the next start.
  const auto reread = d.store.readManifest(ServerId, UserId, d.itemId);
  REQUIRE(reread.has_value());
  CHECK(reread->extrasComplete);
  CHECK(reread->subtitleFiles == after->subtitleFiles);
  // The copy plays with them.
  d.local.setOnline(false);
  const auto copy = d.local.find(d.itemId);
  REQUIRE(copy.has_value());
  CHECK(copy->externalSubtitles.size() == 1);
  CHECK(copy->segments.size() == 2);
  // Every request carried the session's header; nothing is asked twice.
  for (const QByteArray& header : authorizations)
    CHECK(header.contains(Token));
  for (auto it = asked.begin(); it != asked.end(); ++it)
    CHECK(it.value() == 1);
  // The token is in no file of the download folder.
  CHECK(filesContaining(d.store.root(), {Token}).isEmpty());

  // Complete: asked again, nothing is fetched.
  const qsizetype before = server.requests().size();
  extras.fetchMissing();
  waitFor([] { return false; }, 100);
  CHECK(server.requests().size() == before);
}

TEST_CASE("Extras stop at a network error and are asked again later; a 404 means none") {
  int failNext = 1;
  FakeServer server([&](const HttpRequest& r) {
    if (r.path.startsWith(QLatin1String("/MediaSegments/"))) {
      if (failNext-- > 0) return HttpResponse{503, {}};
      return HttpResponse{404, {}};
    }
    return HttpResponse{404, {}};
  });
  Downloaded d;
  auto m = *d.catalog.find(ServerId, UserId, d.itemId);
  m.segmentsFile.clear();
  m.extrasComplete = false;
  REQUIRE(d.store.writeManifest(m));
  d.catalog.update(m);

  ExtrasDownloader extras(d.store, d.catalog);
  QStringList done;
  QObject::connect(&extras, &ExtrasDownloader::finished,
                   [&](const QString&, bool complete) { done.append(QLatin1String(complete ? "+" : "-")); });
  extras.setAccess(ServerId, UserId, server.url(), "MediaBrowser Token=\"x\"");
  extras.fetch(d.itemId);
  REQUIRE(waitFor([&] { return done.size() == 1; }));
  CHECK(done.back() == QStringLiteral("-"));
  CHECK_FALSE(d.catalog.find(ServerId, UserId, d.itemId)->extrasComplete);
  extras.fetchMissing();
  REQUIRE(waitFor([&] { return done.size() == 2; }));
  CHECK(done.back() == QStringLiteral("+"));
  const auto after = d.catalog.find(ServerId, UserId, d.itemId);
  CHECK(after->extrasComplete);
  CHECK(after->segmentsFile.isEmpty());
  // The subtitle file already there is kept, not fetched again.
  CHECK(after->subtitleFiles.value(0) == QStringLiteral("subs/0.srt"));
}

TEST_CASE("Extras are never fetched from another host") {
  FakeServer elsewhere([](const HttpRequest&) { return HttpResponse{200, "x"}; });
  Downloaded d;
  auto m = *d.catalog.find(ServerId, UserId, d.itemId);
  m.segmentsFile.clear();
  m.extrasComplete = false;
  REQUIRE(d.store.writeManifest(m));
  d.catalog.update(m);
  // The server answers with a redirect to another host.
  FakeServer server([&](const HttpRequest&) {
    return HttpResponse{302, {}, "text/plain", 0,
                        {{"Location", elsewhere.url(QStringLiteral("/x")).toEncoded()}}};
  });
  ExtrasDownloader extras(d.store, d.catalog);
  bool finished = false;
  QObject::connect(&extras, &ExtrasDownloader::finished, [&](const QString&, bool) { finished = true; });
  extras.setAccess(ServerId, UserId, server.url(), "MediaBrowser Token=\"" + Token + "\"");
  extras.fetch(d.itemId);
  REQUIRE(waitFor([&] { return finished; }));
  CHECK(elsewhere.requests().isEmpty());
}
