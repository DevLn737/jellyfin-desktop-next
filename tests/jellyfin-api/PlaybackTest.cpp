#include "../support/DoctestQt.h"

#include <jfd/api/Models.h>
#include <jfd/api/Playback.h>

#include <QJsonDocument>
#include <QUrlQuery>

using namespace jfd::api;

namespace {
  // Shaped like a 12.2 PlaybackInfo answer for an MKV with one video, two
  // embedded audio tracks, an embedded and an external subtitle, and an
  // external audio file.
  const char* PlaybackInfoJson = R"({
    "MediaSources": [{
      "Id": "src1", "Protocol": "File", "Path": "/media/Show/S01E01.mkv",
      "Container": "mkv,webm", "ETag": "etag1",
      "SupportsDirectPlay": true, "SupportsDirectStream": true, "SupportsTranscoding": true,
      "TranscodingUrl": "/videos/item1/master.m3u8?MediaSourceId=src1&ApiKey=server-key",
      "TranscodingSubProtocol": "hls", "RunTimeTicks": 14400000000,
      "DefaultAudioStreamIndex": 1, "DefaultSubtitleStreamIndex": 4,
      "RequiredHttpHeaders": {},
      "MediaStreams": [
        {"Index": 0, "Type": "Video", "Codec": "h264", "RealFrameRate": 23.976},
        {"Index": 1, "Type": "Audio", "Codec": "aac", "Language": "jpn", "IsDefault": true},
        {"Index": 2, "Type": "Audio", "Codec": "ac3", "Language": "rus", "Channels": 6},
        {"Index": 3, "Type": "Subtitle", "Codec": "ass", "DeliveryMethod": "Embed"},
        {"Index": 4, "Type": "Subtitle", "Codec": "srt", "IsExternal": true,
         "DeliveryMethod": "External", "Language": "rus", "DisplayTitle": "Russian",
         "DeliveryUrl": "/Videos/item1/src1/Subtitles/4/0/Stream.srt"},
        {"Index": 5, "Type": "Audio", "Codec": "flac", "IsExternal": true, "Language": "eng"}
      ]
    }],
    "PlaySessionId": "ps1"
  })";

  PlaybackInfo parseInfo(const char* text = PlaybackInfoJson) {
    auto info = json::playbackInfo(QJsonDocument::fromJson(text).object());
    REQUIRE(info);
    return info.value();
  }

  StreamContext context() {
    return StreamContext{QUrl(QStringLiteral("https://media.example.org/jellyfin")),
                         QStringLiteral("token1"), QStringLiteral("dev1")};
  }
} // namespace

TEST_CASE("PlaybackInfo parses sources and streams") {
  const PlaybackInfo info = parseInfo();
  CHECK(info.playSessionId == QStringLiteral("ps1"));
  REQUIRE(info.mediaSources.size() == 1);
  const MediaSource& source = info.mediaSources.front();
  CHECK(source.runTimeTicks == 14'400'000'000LL);
  CHECK(source.defaultSubtitleStreamIndex == 4);
  REQUIRE(source.mediaStreams.size() == 6);
  CHECK(source.mediaStreams[0].realFrameRate == doctest::Approx(23.976));
  CHECK(source.mediaStreams[2].channels == 6);
  CHECK(source.mediaStreams[4].isExternal);
}

TEST_CASE("A file source is played as a static stream under the server's path prefix") {
  const PlaybackInfo info = parseInfo();
  auto stream =
      resolvePlaybackStream(context(), QStringLiteral("item1"), info, info.mediaSources[0]);
  REQUIRE(stream);
  CHECK(stream->method == PlayMethod::DirectPlay);
  CHECK_FALSE(stream->isHls);
  CHECK(stream->url.path() == QStringLiteral("/jellyfin/Videos/item1/stream.mkv"));
  const QUrlQuery query(stream->url);
  CHECK(query.queryItemValue(QStringLiteral("Static")) == QStringLiteral("true"));
  CHECK(query.queryItemValue(QStringLiteral("MediaSourceId")) == QStringLiteral("src1"));
  CHECK(query.queryItemValue(QStringLiteral("PlaySessionId")) == QStringLiteral("ps1"));
  CHECK(query.queryItemValue(QStringLiteral("DeviceId")) == QStringLiteral("dev1"));
  CHECK(query.queryItemValue(QStringLiteral("Tag")) == QStringLiteral("etag1"));
  CHECK(query.queryItemValue(QStringLiteral("ApiKey")) == QStringLiteral("token1"));
  CHECK_FALSE(query.hasQueryItem(QStringLiteral("api_key")));
}

TEST_CASE("External subtitles and audio get authenticated URLs") {
  const PlaybackInfo info = parseInfo();
  auto stream =
      resolvePlaybackStream(context(), QStringLiteral("item1"), info, info.mediaSources[0]);
  REQUIRE(stream);
  REQUIRE(stream->externalSubtitles.size() == 1);
  const ExternalTrack& subtitle = stream->externalSubtitles.front();
  CHECK(subtitle.streamIndex == 4);
  CHECK(subtitle.title == QStringLiteral("Russian"));
  CHECK(subtitle.url.toString() == QStringLiteral("https://media.example.org/jellyfin/Videos/item1/"
                                                  "src1/Subtitles/4/0/Stream.srt?ApiKey=token1"));

  REQUIRE(stream->externalAudio.size() == 1);
  const ExternalTrack& audio = stream->externalAudio.front();
  CHECK(audio.streamIndex == 5);
  CHECK(audio.url.path() == QStringLiteral("/jellyfin/Audio/item1/stream.flac"));
  const QUrlQuery query(audio.url);
  CHECK(query.queryItemValue(QStringLiteral("MediaSourceId")) == QStringLiteral("src1"));
  CHECK(query.queryItemValue(QStringLiteral("AudioStreamIndex")) == QStringLiteral("5"));
  CHECK(query.queryItemValue(QStringLiteral("EnableRedirection")) == QStringLiteral("false"));
  CHECK(query.queryItemValue(QStringLiteral("ApiKey")) == QStringLiteral("token1"));
}

TEST_CASE("Without direct play the server's transcode is used as given") {
  PlaybackInfo info = parseInfo();
  MediaSource source = info.mediaSources[0];
  source.supportsDirectPlay = false;
  source.supportsDirectStream = false;
  auto stream = resolvePlaybackStream(context(), QStringLiteral("item1"), info, source);
  REQUIRE(stream);
  CHECK(stream->method == PlayMethod::Transcode);
  CHECK(stream->isHls);
  CHECK(stream->url.path() == QStringLiteral("/jellyfin/videos/item1/master.m3u8"));
  // The server's key is kept and no second one is added.
  const QUrlQuery query(stream->url);
  CHECK(query.allQueryItemValues(QStringLiteral("ApiKey")) ==
        QStringList{QStringLiteral("server-key")});
}

TEST_CASE("Remote HTTP sources play directly unless they need headers") {
  PlaybackInfo info = parseInfo();
  MediaSource source = info.mediaSources[0];
  source.protocol = QStringLiteral("Http");
  source.path = QStringLiteral("https://cdn.example.com/video.mkv");
  auto direct = resolvePlaybackStream(context(), QStringLiteral("item1"), info, source);
  REQUIRE(direct);
  CHECK(direct->url == QUrl(QStringLiteral("https://cdn.example.com/video.mkv")));
  CHECK(direct->method == PlayMethod::DirectPlay);

  source.requiredHttpHeaders.insert(QStringLiteral("Referer"), QStringLiteral("x"));
  auto transcoded = resolvePlaybackStream(context(), QStringLiteral("item1"), info, source);
  REQUIRE(transcoded);
  CHECK(transcoded->method == PlayMethod::Transcode);

  source.supportsTranscoding = false;
  auto none = resolvePlaybackStream(context(), QStringLiteral("item1"), info, source);
  REQUIRE_FALSE(none);
  CHECK(none.error().problemCode == QStringLiteral("RequiredHttpHeaders"));
}

TEST_CASE("A PlaybackInfo error code is reported, not played") {
  const PlaybackInfo info =
      parseInfo(R"({"MediaSources": [], "PlaySessionId": "", "ErrorCode": "NotAllowed"})");
  auto stream = resolvePlaybackStream(context(), QStringLiteral("item1"), info, MediaSource{});
  REQUIRE_FALSE(stream);
  CHECK(stream.error().kind == ApiError::Kind::Unsupported);
  CHECK(stream.error().problemCode == QStringLiteral("NotAllowed"));
}

TEST_CASE("mpv track ids count embedded streams of one type in list order") {
  const MediaSource source = parseInfo().mediaSources[0];
  CHECK(embeddedTrackId(source, 0) == 1);
  CHECK(embeddedTrackId(source, 1) == 1);
  CHECK(embeddedTrackId(source, 2) == 2);
  CHECK(embeddedTrackId(source, 3) == 1);
  CHECK_FALSE(embeddedTrackId(source, 4));
  CHECK_FALSE(embeddedTrackId(source, 5));
  CHECK_FALSE(embeddedTrackId(source, 99));
}

TEST_CASE("Playback report bodies follow the server DTOs") {
  PlaybackProgress progress;
  progress.itemId = QStringLiteral("item1");
  progress.mediaSourceId = QStringLiteral("src1");
  progress.playSessionId = QStringLiteral("ps1");
  progress.positionTicks = 123'456'789'012LL;
  progress.playMethod = PlayMethod::Transcode;
  progress.isPaused = true;
  progress.audioStreamIndex = 2;
  progress.subtitleStreamIndex = -1;
  progress.playbackRate = 1.5;
  progress.eventName = QStringLiteral("Pause");

  const QJsonObject body = playbackProgressBody(progress);
  CHECK(body.value(QStringLiteral("PositionTicks")).toInteger() == 123'456'789'012LL);
  CHECK(body.value(QStringLiteral("PlayMethod")).toString() == QStringLiteral("Transcode"));
  CHECK(body.value(QStringLiteral("IsPaused")).toBool());
  CHECK(body.value(QStringLiteral("SubtitleStreamIndex")).toInt() == -1);
  CHECK(body.value(QStringLiteral("PlaybackRate")).toDouble() == doctest::Approx(1.5));
  CHECK(body.value(QStringLiteral("EventName")).toString() == QStringLiteral("Pause"));

  const QJsonObject stopped = playbackStoppedBody(progress, true);
  CHECK(stopped.value(QStringLiteral("Failed")).toBool());
  CHECK_FALSE(stopped.contains(QStringLiteral("IsPaused")));
}

TEST_CASE("Item parsing tolerates missing optional fields and rejects a missing Id") {
  auto episode = json::item(QJsonDocument::fromJson(R"({
    "Id": "e1", "Name": "Pilot", "Type": "Episode", "SeriesId": "s1", "SeasonId": "se1",
    "IndexNumber": 1, "ParentIndexNumber": 2, "RunTimeTicks": 13000000000,
    "ImageTags": {"Primary": "tag1"},
    "UserData": {"PlaybackPositionTicks": 50000000, "Played": false, "PlayCount": 0}
  })")
                                .object());
  REQUIRE(episode);
  CHECK(episode->indexNumber == 1);
  CHECK(episode->parentIndexNumber == 2);
  CHECK(episode->primaryImageTag == QStringLiteral("tag1"));
  CHECK(episode->userData.playbackPositionTicks == 50'000'000);
  CHECK_FALSE(episode->productionYear);

  CHECK_FALSE(json::item(QJsonDocument::fromJson(R"({"Name": "x"})").object()));
  CHECK_FALSE(json::itemPage(QJsonDocument::fromJson(R"({"TotalRecordCount": 1})").object()));
}
