#include "../support/DoctestQt.h"
#include "FakeTranscodeServer.h"
#include "OfflineFixtures.h"

#include <jfd/api/Downloads.h>
#include <jfd/offline/CompressedDownload.h>
#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadSettings.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/ExtrasDownloader.h>
#include <jfd/offline/HlsPlaylist.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/OfflineLocalSource.h>
#include <jfd/settings/SettingsRegistry.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

// Compressed copies (3.10b) against a fake HLS VOD transcode
// (FakeTranscodeServer.h): the download profile, the segments joined into
// one file, resuming after a dropped request and after a restart, starting
// over when the server's transcode differs, refusals, one transcode at a
// time, and what the finished copy's manifest says.

using namespace jfd;
using namespace jfd::offline;
using namespace jfd::test;

namespace {

  constexpr qint64 TwoMinutesTicks = 1'200'000'000;

  QJsonObject clipSource(const QString& id) {
    const QString secret = QString::fromLatin1(Token);
    const auto stream = [](int index, const char* type, const char* codec, bool external = false,
                           const char* language = nullptr) {
      QJsonObject o{{QStringLiteral("Index"), index},
                    {QStringLiteral("Type"), QLatin1String(type)},
                    {QStringLiteral("Codec"), QLatin1String(codec)},
                    {QStringLiteral("IsExternal"), external}};
      if (language) o.insert(QStringLiteral("Language"), QLatin1String(language));
      return o;
    };
    QJsonObject video = stream(0, "Video", "h264");
    video.insert(QStringLiteral("Width"), 1920);
    video.insert(QStringLiteral("Height"), 1080);
    QJsonObject embeddedSrt = stream(3, "Subtitle", "subrip", false, "eng");
    embeddedSrt.insert(QStringLiteral("DeliveryUrl"),
                       QStringLiteral("/Videos/%1/%1/Subtitles/3/0/Stream.srt?ApiKey=%2").arg(id, secret));
    return QJsonObject{
        {QStringLiteral("Id"), id},
        {QStringLiteral("Protocol"), QStringLiteral("File")},
        {QStringLiteral("Path"), QStringLiteral("/media/clips/Alice.mkv")},
        {QStringLiteral("Container"), QStringLiteral("mkv")},
        {QStringLiteral("Size"), 900000000.0},
        {QStringLiteral("Bitrate"), 60000000},
        {QStringLiteral("ETag"), QStringLiteral("c0ffee")},
        {QStringLiteral("RunTimeTicks"), static_cast<double>(TwoMinutesTicks)},
        {QStringLiteral("TranscodingUrl"),
         QStringLiteral("/videos/%1/master.m3u8?ApiKey=%2").arg(id, secret)},
        {QStringLiteral("MediaStreams"),
         QJsonArray{video, stream(1, "Audio", "ac3", false, "eng"), stream(2, "Audio", "aac", false, "rus"),
                    embeddedSrt, stream(4, "Subtitle", "PGSSUB", false, "eng"),
                    stream(5, "Subtitle", "ass", true, "rus"), stream(6, "Attachment", "ttf")}},
    };
  }

  struct Harness {
    QTemporaryDir dir;
    DownloadStore store{dir.filePath(QStringLiteral("downloads"))};
    OfflineCatalog catalog{store};
    std::shared_ptr<FakeStorageGuard> guard = std::make_shared<FakeStorageGuard>(0, 0);
    std::unique_ptr<DownloadQueue> queue;
    QUrl serverUrl;
    QStringList completed;
    QList<QPair<QString, QueueProblem>> failures;

    explicit Harness(const QUrl& url) : serverUrl(url) { open(); }

    void open(int parallel = 1) {
      queue.reset();
      catalog.load();
      queue = std::make_unique<DownloadQueue>(store, catalog, guard);
      queue->setRetryDelaysMs({10, 10, 10, 10, 10});
      queue->setTranscodeTimeoutMs(1000);
      queue->setMaxParallel(parallel);
      QObject::connect(queue.get(), &DownloadQueue::completed,
                       [this](const QString& id) { completed.append(id); });
      QObject::connect(queue.get(), &DownloadQueue::failed,
                       [this](const QString& id, QueueProblem p) { failures.append({id, p}); });
      queue->setAccess(ServerId, UserId, serverUrl, "MediaBrowser Token=\"" + Token + "\"");
    }

    EnqueueResult add(const QString& id, const QString& preset = QStringLiteral("video-1080p"),
                      std::optional<int> audio = std::nullopt) {
      const QJsonObject source = clipSource(id);
      QJsonObject item = itemJson(id, source, QStringLiteral("Video"));
      item.insert(QStringLiteral("RunTimeTicks"), static_cast<double>(TwoMinutesTicks));
      return queue->enqueueCompressed(item, source, preset, audio);
    }

    std::optional<ItemManifest> manifest(const QString& id) const {
      return store.readManifest(ServerId, UserId, id);
    }
    QByteArray media(const QString& id) const {
      const auto m = manifest(id);
      if (!m) return {};
      QFile file(store.mediaPath(*m));
      return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
  };

  QJsonObject streamOf(const QJsonObject& source, int index) {
    for (const QJsonValue& v : source.value(QLatin1String("MediaStreams")).toArray())
      if (v.toObject().value(QLatin1String("Index")).toInt() == index) return v.toObject();
    return {};
  }

} // namespace

TEST_CASE("Presets: 1080p and 720p HEVC, Opus for music; the original stays the default") {
  CHECK(compressedPresets().size() == 3);
  const auto p1080 = compressedPreset(QStringLiteral("video-1080p"));
  REQUIRE(p1080);
  CHECK(p1080->profile.maxHeight == 1080);
  CHECK(p1080->profile.bitrate == 4'000'000);
  const auto p720 = compressedPreset(QStringLiteral("video-720p"));
  REQUIRE(p720);
  CHECK(p720->profile.maxHeight == 720);
  CHECK(p720->profile.bitrate == 2'000'000);
  const auto opus = compressedPreset(QStringLiteral("audio-opus128"));
  REQUIRE(opus);
  CHECK(opus->profile.audio);
  CHECK(opus->profile.bitrate == 128'000);

  CHECK_FALSE(presetForQuality(QStringLiteral("original"), false));
  CHECK_FALSE(presetForQuality(QStringLiteral("original"), true));
  CHECK(presetForQuality(QStringLiteral("video-720p"), false)->id == QStringLiteral("video-720p"));
  CHECK(presetForQuality(QStringLiteral("video-720p"), true)->id == QStringLiteral("audio-opus128"));
  CHECK(presetForQuality(QStringLiteral("nonsense"), false)->id == QStringLiteral("video-1080p"));

  // Two minutes at 4 Mbit/s: 60,000,000 bytes.
  CHECK(api::estimatedCompressedSize(p1080->profile, TwoMinutesTicks) == 60'000'000);

  settings::SettingsRegistry registry;
  REQUIRE(registerDownloadSettings(registry));
  const auto* setting = registry.setting(QString(keys::Quality));
  REQUIRE(setting);
  CHECK(setting->defaultValue.toString() == QStringLiteral("original"));
  REQUIRE(setting->choices.size() == 3);
  CHECK(setting->choices.at(1).value == QStringLiteral("video-1080p"));
  CHECK(setting->choices.at(2).value == QStringLiteral("video-720p"));
}

TEST_CASE("The download profile: HLS fMP4 only, height and width capped, subtitles as files") {
  const QJsonObject profile = api::compressedDeviceProfile({false, 720, 2'000'000});
  CHECK(profile.value(QStringLiteral("DirectPlayProfiles")).toArray().isEmpty());
  const QJsonArray transcoding = profile.value(QStringLiteral("TranscodingProfiles")).toArray();
  REQUIRE(transcoding.size() == 1);
  const QJsonObject t = transcoding.first().toObject();
  CHECK(t.value(QStringLiteral("Protocol")).toString() == QStringLiteral("hls"));
  CHECK(t.value(QStringLiteral("Context")).toString() == QStringLiteral("Streaming"));
  CHECK(t.value(QStringLiteral("Container")).toString() == QStringLiteral("mp4"));
  CHECK(t.value(QStringLiteral("VideoCodec")).toString() == QStringLiteral("hevc,h264"));
  CHECK(profile.value(QStringLiteral("MaxStreamingBitrate")).toDouble() == 2'000'000);
  const QString text = QString::fromUtf8(QJsonDocument(profile).toJson(QJsonDocument::Compact));
  CHECK(text.contains(QStringLiteral("\"Property\":\"Height\",\"Value\":\"720\"")));
  CHECK(text.contains(QStringLiteral("\"Property\":\"Width\",\"Value\":\"1280\"")));
  for (const QJsonValue& v : profile.value(QStringLiteral("SubtitleProfiles")).toArray())
    CHECK(v.toObject().value(QStringLiteral("Method")).toString() == QStringLiteral("External"));

  const QJsonObject music = api::compressedDeviceProfile({true, 0, 128'000});
  const QJsonObject m = music.value(QStringLiteral("TranscodingProfiles")).toArray().first().toObject();
  CHECK(m.value(QStringLiteral("Type")).toString() == QStringLiteral("Audio"));
  CHECK(m.value(QStringLiteral("AudioCodec")).toString() == QStringLiteral("opus"));

  const QJsonObject body =
      api::compressedPlaybackInfoBody(UserId, QStringLiteral("abc"), {false, 1080, 4'000'000}, 2);
  CHECK(body.value(QStringLiteral("MediaSourceId")).toString() == QStringLiteral("abc"));
  CHECK(body.value(QStringLiteral("AudioStreamIndex")).toInt() == 2);
  CHECK(body.value(QStringLiteral("SubtitleStreamIndex")).toInt() == -1);
  CHECK(body.value(QStringLiteral("EnableDirectPlay")).toBool() == false);
  CHECK(body.value(QStringLiteral("AllowVideoStreamCopy")).toBool() == false);
  CHECK(body.value(QStringLiteral("MaxStreamingBitrate")).toDouble() == 4'000'000);

  // The TranscodingUrl under the server's base path, without its ApiKey.
  const QUrl url = api::transcodingUrlOnServer(
      QUrl(QStringLiteral("https://media.example:8920/jf/")),
      QStringLiteral("/videos/abc/master.m3u8?DeviceId=d&ApiKey=s3cret&PlaySessionId=p"));
  CHECK(url.toString() ==
        QStringLiteral("https://media.example:8920/jf/videos/abc/master.m3u8?DeviceId=d&PlaySessionId=p"));
  CHECK(api::transcodingUrlOnServer(QUrl(QStringLiteral("https://media.example/")),
                                    QStringLiteral("https://elsewhere.example/videos/abc/master.m3u8"))
            .isEmpty());
}

TEST_CASE("HLS playlists: a complete VOD playlist is read; live and event playlists are not") {
  const QByteArray master =
      "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=3936000\nmain.m3u8?DeviceId=d&PlaySessionId=p\n";
  CHECK(hlsFirstVariant(master) == QStringLiteral("main.m3u8?DeviceId=d&PlaySessionId=p"));
  CHECK(hlsFirstVariant("not a playlist").isEmpty());

  const QByteArray vod = "#EXTM3U\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-TARGETDURATION:6\n"
                         "#EXT-X-MAP:URI=\"hls1/main/-1.mp4?a=1\"\n"
                         "#EXTINF:6.000000, nodesc\nhls1/main/0.mp4?a=1\n"
                         "#EXTINF:0.023000, nodesc\nhls1/main/1.mp4?a=1\n#EXT-X-ENDLIST\n";
  const auto playlist = parseHlsMediaPlaylist(vod);
  REQUIRE(playlist);
  CHECK(playlist->mapUri == QStringLiteral("hls1/main/-1.mp4?a=1"));
  REQUIRE(playlist->segments.size() == 2);
  CHECK(playlist->segments.at(1).uri == QStringLiteral("hls1/main/1.mp4?a=1"));
  CHECK(playlist->totalSeconds == doctest::Approx(6.023));
  // The same durations in another play session: the same signature.
  QByteArray other = vod;
  other.replace("a=1", "a=2");
  CHECK(parseHlsMediaPlaylist(other)->signature() == playlist->signature());
  QByteArray longer = vod;
  longer.replace("#EXTINF:0.023000", "#EXTINF:0.024000");
  CHECK(parseHlsMediaPlaylist(longer)->signature() != playlist->signature());

  QByteArray live = vod;
  live.replace("#EXT-X-ENDLIST\n", "");
  CHECK_FALSE(parseHlsMediaPlaylist(live));
  QByteArray event = vod;
  event.replace("VOD", "EVENT");
  CHECK_FALSE(parseHlsMediaPlaylist(event));
  CHECK_FALSE(parseHlsMediaPlaylist("#EXTM3U\n#EXT-X-ENDLIST\n"));
}

TEST_CASE("A compressed copy: segments joined into one file, the manifest describes the copy") {
  const QString id = itemIdFor(301);
  FakeTranscodeServer server({id});
  Harness h(server.serverUrl());
  REQUIRE(h.add(id) == EnqueueResult::Queued);
  {
    const auto queued = h.manifest(id);
    REQUIRE(queued);
    CHECK(queued->quality == QStringLiteral("compressed"));
    CHECK(queued->transcode.preset == QStringLiteral("video-1080p"));
    // 4 Mbit/s for two minutes until the copy is done.
    CHECK(queued->expectedSize == 60'000'000);
  }
  REQUIRE(waitFor([&] { return !h.completed.isEmpty() || !h.failures.isEmpty(); }, 10000));
  REQUIRE(h.completed == QStringList{id});
  CHECK(h.media(id) == server.expectedFile());

  // PlaybackInfo with the download profile; the server picked the audio.
  REQUIRE(server.playbackInfoBodies.size() == 1);
  const QJsonObject body = server.playbackInfoBodies.first();
  CHECK(body.value(QStringLiteral("MediaSourceId")).toString() == id);
  CHECK_FALSE(body.contains(QStringLiteral("AudioStreamIndex")));
  CHECK(body.value(QStringLiteral("MaxStreamingBitrate")).toDouble() == 4'000'000);
  CHECK(server.segmentsOf(1) == QList<int>{0, 1, 2, 3, 4, 5});
  // Every request carried the session's header; none carried the ApiKey.
  for (const HttpRequest& r : server.http().requests()) {
    CHECK(r.headers.value("authorization").contains(Token));
    CHECK_FALSE(r.query.hasQueryItem(QStringLiteral("ApiKey")));
  }
  // The server's job for this play session was stopped.
  REQUIRE(waitFor([&] { return !server.stops.isEmpty(); }, 3000));
  CHECK(server.stops.first().query.queryItemValue(QStringLiteral("playSessionId")) == QStringLiteral("ps1"));
  CHECK(server.stops.first().query.queryItemValue(QStringLiteral("deviceId")) == QStringLiteral("dev-1"));

  const auto m = h.manifest(id);
  REQUIRE(m);
  CHECK(m->state == ItemState::Complete);
  CHECK(m->mediaFile.endsWith(QStringLiteral(".mp4")));
  CHECK(m->format == 2);
  CHECK(m->expectedSize == server.expectedFile().size());
  CHECK(m->transcode.preset == QStringLiteral("video-1080p"));
  CHECK(m->transcode.audioStreamIndex == 2);
  CHECK(m->transcode.videoCodec == QStringLiteral("hevc"));
  CHECK(m->transcode.audioCodec == QStringLiteral("aac"));
  CHECK(m->transcode.segmentsDone == 0);
  const QJsonObject source = m->mediaSource;
  CHECK(source.value(QStringLiteral("Id")).toString() == id);
  CHECK(source.value(QStringLiteral("Container")).toString() == QStringLiteral("mp4"));
  CHECK(source.value(QStringLiteral("Size")).toDouble() == server.expectedFile().size());
  CHECK(source.value(QStringLiteral("Bitrate")).toDouble() == 4'000'000);
  CHECK(source.value(QStringLiteral("DefaultAudioStreamIndex")).toInt() == 2);
  CHECK(source.value(QStringLiteral("ETag")).toString() == QStringLiteral("c0ffee"));
  QList<int> indexes;
  for (const QJsonValue& v : source.value(QStringLiteral("MediaStreams")).toArray())
    indexes.append(v.toObject().value(QStringLiteral("Index")).toInt());
  // Video, the one audio track, the text subtitles; no PGS, no fonts.
  CHECK(indexes == QList<int>{0, 2, 3, 5});
  CHECK(streamOf(source, 0).value(QStringLiteral("Codec")).toString() == QStringLiteral("hevc"));
  CHECK_FALSE(streamOf(source, 0).contains(QStringLiteral("Height")));
  CHECK(streamOf(source, 2).value(QStringLiteral("Codec")).toString() == QStringLiteral("aac"));
  CHECK(streamOf(source, 3).value(QStringLiteral("IsExternal")).toBool());
  CHECK_FALSE(streamOf(source, 3).contains(QStringLiteral("DeliveryUrl")));

  // Never stored: the token, ApiKey, TranscodingUrl, DirectStreamUrl, the
  // play session's URLs.
  CHECK(filesContaining(h.store.root(), {Token, "apikey", "api_key", "TranscodingUrl",
                                         "DirectStreamUrl", "PlaySessionId", "master.m3u8"})
            .isEmpty());
}

TEST_CASE("A compressed copy's text subtitles, embedded ones too, become files; it plays from disk") {
  const QString id = itemIdFor(302);
  FakeTranscodeServer server({id});
  Harness h(server.serverUrl());
  REQUIRE(h.add(id, QStringLiteral("video-720p"), 1) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty(); }, 10000));
  // The chosen track was asked for, and kept.
  CHECK(server.playbackInfoBodies.first().value(QStringLiteral("AudioStreamIndex")).toInt() == 1);
  CHECK(h.manifest(id)->transcode.audioStreamIndex == 1);

  ExtrasDownloader extras(h.store, h.catalog);
  extras.setAccess(ServerId, UserId, h.serverUrl, "MediaBrowser Token=\"" + Token + "\"");
  bool done = false;
  QObject::connect(&extras, &ExtrasDownloader::finished, [&](const QString&, bool) { done = true; });
  extras.fetch(id);
  REQUIRE(waitFor([&] { return done; }, 5000));
  server.subtitleRequests.sort();
  CHECK(server.subtitleRequests == QStringList{QStringLiteral("3/Stream.srt"), QStringLiteral("5/Stream.ass")});

  OfflineLocalSource local(h.store, h.catalog);
  local.setUser(ServerId, UserId);
  local.setOnline(false);
  const auto copy = local.find(id);
  REQUIRE(copy);
  CHECK_FALSE(copy->original);
  CHECK(copy->path.endsWith(QStringLiteral(".mp4")));
  CHECK(copy->source.container == QStringLiteral("mp4"));
  REQUIRE(copy->externalSubtitles.size() == 2);
  CHECK(copy->externalSubtitles.at(0).streamIndex == 3);
  CHECK(copy->externalSubtitles.at(0).url.isLocalFile());
  int audio = 0;
  for (const api::MediaStream& s : copy->source.mediaStreams)
    if (s.type == QLatin1String("Audio")) ++audio;
  CHECK(audio == 1);
}

TEST_CASE("A dropped segment is asked again; after a restart the copy continues after the last whole segment") {
  const QString id = itemIdFor(303);
  FakeTranscodeServer server({id});
  int failures503 = 0;
  bool stallFour = true;
  server.segmentHook = [&](int segment, int) -> std::optional<HttpResponse> {
    // Segment 2 fails twice (a dropped connection, a busy server).
    if (segment == 2 && failures503 < 2) {
      ++failures503;
      return HttpResponse{503, {}, {}};
    }
    // Segment 4 never comes: the app is closed while it waits for it.
    if (segment == 4 && stallFour) return HttpResponse{200, server.segmentData(4), "video/mp4", 60000};
    return std::nullopt;
  };
  Harness h(server.serverUrl());
  REQUIRE(h.add(id) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] {
    const auto m = h.manifest(id);
    return m && m->transcode.segmentsDone == 4;
  }, 10000));
  CHECK(failures503 == 2);
  CHECK(h.failures.isEmpty());
  // Each retry is a new play session that continues at segment 2.
  REQUIRE(server.sessions >= 3);
  CHECK(server.segmentsOf(1) == QList<int>{0, 1, 2});
  CHECK(server.segmentsOf(2) == QList<int>{2});
  CHECK(server.segmentsOf(3).value(0) == 2);
  CHECK(server.segmentsOf(3).value(1) == 3);

  // The app quits in the middle of segment 4 and starts again.
  const int sessionsBefore = server.sessions;
  stallFour = false;
  h.open();
  REQUIRE(waitFor([&] { return !h.completed.isEmpty() || !h.failures.isEmpty(); }, 10000));
  REQUIRE(h.completed == QStringList{id});
  // A new play session that asked only for the segments not yet on disk.
  CHECK(server.sessions == sessionsBefore + 1);
  CHECK(server.segmentsOf(server.sessions) == QList<int>{4, 5});
  CHECK(h.media(id) == server.expectedFile());
}

TEST_CASE("A different transcode after a restart, or a damaged .part, starts the copy over") {
  const QString id = itemIdFor(304);
  FakeTranscodeServer server({id});
  bool stall = true;
  server.segmentHook = [&](int segment, int) -> std::optional<HttpResponse> {
    if (segment == 3 && stall) return HttpResponse{200, server.segmentData(3), "video/mp4", 60000};
    return std::nullopt;
  };
  Harness h(server.serverUrl());
  REQUIRE(h.add(id) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] {
    const auto m = h.manifest(id);
    return m && m->transcode.segmentsDone == 3;
  }, 10000));

  SUBCASE("other segment durations") {
    server.lastSeconds = 3.0;
  }
  SUBCASE("another init segment (the server's settings changed)") {
    server.initVariant = "v2";
  }
  SUBCASE("a .part shorter than the manifest says") {
    h.queue.reset();
    const auto m = h.manifest(id);
    REQUIRE(m);
    QFile::resize(h.store.partPath(*m), 100);
  }
  stall = false;
  h.open();
  REQUIRE(waitFor([&] { return !h.completed.isEmpty() || !h.failures.isEmpty(); }, 10000));
  REQUIRE(h.completed == QStringList{id});
  CHECK(server.segmentsOf(server.sessions) == QList<int>{0, 1, 2, 3, 4, 5});
  CHECK(h.media(id) == server.expectedFile());
}

TEST_CASE("Refusals: no transcode offered fails the item; 401 stops the queue; an event playlist fails") {
  const QString id = itemIdFor(305);
  FakeTranscodeServer server({id});
  Harness h(server.serverUrl());

  SUBCASE("no TranscodingUrl (the user may not transcode)") {
    server.playbackInfoHook = [](const HttpRequest&) -> std::optional<HttpResponse> {
      return HttpResponse{200, R"({"MediaSources":[{"Id":"x","SupportsTranscoding":false}]})"};
    };
    REQUIRE(h.add(id) == EnqueueResult::Queued);
    REQUIRE(waitFor([&] { return !h.failures.isEmpty(); }, 5000));
    CHECK(h.failures.first().second == QueueProblem::Refused);
    CHECK(server.playbackInfoBodies.size() == 1);
  }
  SUBCASE("ErrorCode NotAllowed") {
    server.playbackInfoHook = [](const HttpRequest&) -> std::optional<HttpResponse> {
      return HttpResponse{200, R"({"ErrorCode":"NotAllowed","MediaSources":[]})"};
    };
    REQUIRE(h.add(id) == EnqueueResult::Queued);
    REQUIRE(waitFor([&] { return !h.failures.isEmpty(); }, 5000));
    CHECK(h.failures.first().second == QueueProblem::Refused);
  }
  SUBCASE("401") {
    server.playbackInfoHook = [](const HttpRequest&) -> std::optional<HttpResponse> {
      return HttpResponse{401, {}, {}};
    };
    REQUIRE(h.add(id) == EnqueueResult::Queued);
    REQUIRE(waitFor([&] { return h.queue->problem() == QueueProblem::SignInRequired; }, 5000));
  }
  SUBCASE("an event playlist") {
    server.mediaPlaylistOverride =
        "#EXTM3U\n#EXT-X-PLAYLIST-TYPE:EVENT\n#EXTINF:6.0,\nhls1/main/0.mp4\n";
    REQUIRE(h.add(id) == EnqueueResult::Queued);
    REQUIRE(waitFor([&] { return !h.failures.isEmpty(); }, 5000));
    CHECK(h.failures.first().second == QueueProblem::Refused);
    CHECK(server.segmentRequests.isEmpty());
  }
  // A preset that does not fit the item is not queued.
  CHECK(h.add(itemIdFor(306), QStringLiteral("audio-opus128")) == EnqueueResult::InvalidItem);
  CHECK(h.add(itemIdFor(307), QStringLiteral("video-480p")) == EnqueueResult::InvalidItem);
}

TEST_CASE("Compressed copies run one at a time even with three downloads at once") {
  const QString x = itemIdFor(308);
  const QString y = itemIdFor(309);
  FakeTranscodeServer server({x, y});
  server.segmentHook = [&](int segment, int) -> std::optional<HttpResponse> {
    // Slow segments: two transcodes would overlap.
    return HttpResponse{200, server.segmentData(segment), "video/mp4", 30};
  };
  Harness h(server.serverUrl());
  h.open(3);
  REQUIRE(h.add(x) == EnqueueResult::Queued);
  REQUIRE(h.add(y) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] { return h.completed.size() == 2 || !h.failures.isEmpty(); }, 10000));
  CHECK(h.failures.isEmpty());
  // y's transcode was asked for only after x's last segment.
  const qsizetype lastOfX = server.log.indexOf(x + QStringLiteral(" 5"));
  const qsizetype startOfY = server.log.indexOf(y + QStringLiteral(" PlaybackInfo"));
  REQUIRE(lastOfX >= 0);
  CHECK(startOfY > lastOfX);
}

TEST_CASE("Pausing a compressed copy stops the server's job; resuming continues; cancelling removes it") {
  const QString id = itemIdFor(312);
  FakeTranscodeServer server({id});
  bool stall = true;
  server.segmentHook = [&](int segment, int) -> std::optional<HttpResponse> {
    if (segment == 3 && stall) return HttpResponse{200, server.segmentData(3), "video/mp4", 60000};
    return std::nullopt;
  };
  Harness h(server.serverUrl());
  h.queue->setTranscodeTimeoutMs(30000);
  REQUIRE(h.add(id) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] {
    const auto m = h.manifest(id);
    return m && m->transcode.segmentsDone == 3;
  }, 10000));
  h.queue->pause(id);
  REQUIRE(waitFor([&] { return h.queue->status(id) && h.queue->status(id)->state == ItemState::Paused; }, 5000));
  REQUIRE(waitFor([&] { return !server.stops.isEmpty(); }, 5000));
  CHECK(server.stops.first().query.queryItemValue(QStringLiteral("playSessionId")) == QStringLiteral("ps1"));

  stall = false;
  h.queue->resume(id);
  REQUIRE(waitFor([&] { return !h.completed.isEmpty(); }, 10000));
  CHECK(server.segmentsOf(2) == QList<int>{3, 4, 5});
  CHECK(h.media(id) == server.expectedFile());

  // Cancelling another one mid-way leaves nothing behind.
  const QString other = itemIdFor(313);
  server.itemIds.append(other);
  stall = true;
  REQUIRE(h.add(other) == EnqueueResult::Queued);
  REQUIRE(waitFor([&] {
    const auto m = h.manifest(other);
    return m && m->transcode.segmentsDone == 3;
  }, 10000));
  const QString folder = h.store.itemDir(*h.manifest(other));
  h.queue->cancel(other);
  REQUIRE(waitFor([&] { return !h.queue->status(other); }, 5000));
  CHECK_FALSE(QFileInfo::exists(folder));
}
