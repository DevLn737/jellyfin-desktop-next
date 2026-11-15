#pragma once

#include "../support/FakeServer.h"
#include "OfflineFixtures.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QStringList>

#include <functional>
#include <optional>

// A server's HLS VOD transcode as Jellyfin 12.2 answers it (see
// docs/offline.md, "Compressed copies"), under the base path /jf:
// POST Items/{id}/PlaybackInfo gives a TranscodingUrl to master.m3u8 with a
// new PlaySessionId and an ApiKey; master.m3u8 names main.m3u8, a complete
// VOD playlist (EXT-X-MAP, EXTINF, EXT-X-ENDLIST) of `segmentCount`
// segments; the segments' bytes depend only on their number (and
// `initVariant` for the init segment), so a copy joined from two play
// sessions is the same as one from a single session, as on 12.2.
namespace jfd::test {

  class FakeTranscodeServer {
  public:
    // The items it transcodes, all alike.
    QStringList itemIds;
    int segmentCount = 6;
    // Seconds of every segment but the last.
    double segmentSeconds = 6.0;
    double lastSeconds = 2.5;
    int segmentBytes = 40 * 1024;
    QByteArray initVariant = "v1";
    // What the server picks when PlaybackInfo names no audio track.
    int defaultAudioIndex = 2;
    // An answer instead of the segment's (a 503, a stall), or nullopt.
    std::function<std::optional<HttpResponse>(int segment, int session)> segmentHook;
    // PlaybackInfo's answer instead of the transcode (a refusal).
    std::function<std::optional<HttpResponse>(const HttpRequest&)> playbackInfoHook;
    // Playlist instead of the VOD one (an event playlist).
    QByteArray mediaPlaylistOverride;

    int sessions = 0;
    QList<QJsonObject> playbackInfoBodies;
    // (segment, session) of every segment request.
    QList<QPair<int, int>> segmentRequests;
    QList<HttpRequest> stops;
    QStringList subtitleRequests;
    // "<item> PlaybackInfo" and "<item> <segment>", in order.
    QStringList log;

    explicit FakeTranscodeServer(QStringList ids) : itemIds(std::move(ids)) {
      m_server.setHandler([this](const HttpRequest& r) { return answer(r); });
    }

    QUrl serverUrl() const { return m_server.url(QStringLiteral("/jf")); }
    FakeServer& http() { return m_server; }

    QByteArray initBytes() const { return QByteArrayLiteral("ftypINIT-") + initVariant; }
    QByteArray segmentData(int n) const {
      QByteArray data(segmentBytes, Qt::Uninitialized);
      for (int i = 0; i < data.size(); ++i)
        data[i] = static_cast<char>((i * 7 + n * 13) & 0xff);
      return data;
    }
    QByteArray expectedFile() const {
      QByteArray all = initBytes();
      for (int n = 0; n < segmentCount; ++n)
        all += segmentData(n);
      return all;
    }
    // Segments requested in one play session (1-based).
    QList<int> segmentsOf(int session) const {
      QList<int> out;
      for (const auto& [segment, s] : segmentRequests)
        if (s == session) out.append(segment);
      return out;
    }

  private:
    static int sessionOf(const HttpRequest& r) {
      return r.query.queryItemValue(QStringLiteral("PlaySessionId")).mid(2).toInt();
    }

    QByteArray mediaPlaylist(int session) const {
      if (!mediaPlaylistOverride.isEmpty()) return mediaPlaylistOverride;
      const QByteArray query = "?DeviceId=dev-1&PlaySessionId=ps" + QByteArray::number(session);
      QByteArray out = "#EXTM3U\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:6\n"
                       "#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-MAP:URI=\"hls1/main/-1.mp4" +
                       query + "\"\n";
      for (int n = 0; n < segmentCount; ++n) {
        const double seconds = n + 1 == segmentCount ? lastSeconds : segmentSeconds;
        out += "#EXTINF:" + QByteArray::number(seconds, 'f', 6) + ", nodesc\nhls1/main/" +
               QByteArray::number(n) + ".mp4" + query + "\n";
      }
      return out + "#EXT-X-ENDLIST\n";
    }

    HttpResponse answer(const HttpRequest& r) {
      if (r.method == "DELETE" && r.path == QStringLiteral("/jf/Videos/ActiveEncodings")) {
        stops.append(r);
        return {204, {}, {}};
      }
      for (const QString& id : std::as_const(itemIds))
        if (r.path.contains(id)) return answer(r, id);
      return {404, {}, {}};
    }

    HttpResponse answer(const HttpRequest& r, const QString& itemId) {
      const QString videos = QStringLiteral("/jf/videos/") + itemId + QLatin1Char('/');
      if (r.method == "POST" && r.path == QStringLiteral("/jf/Items/") + itemId + QStringLiteral("/PlaybackInfo")) {
        const QJsonObject body = QJsonDocument::fromJson(r.body).object();
        playbackInfoBodies.append(body);
        log.append(itemId + QStringLiteral(" PlaybackInfo"));
        if (playbackInfoHook)
          if (auto answer = playbackInfoHook(r)) return *answer;
        const int session = ++sessions;
        const int audio = body.contains(QLatin1String("AudioStreamIndex"))
                              ? body.value(QLatin1String("AudioStreamIndex")).toInt()
                              : defaultAudioIndex;
        const QString url =
            QStringLiteral("/videos/%1/master.m3u8?DeviceId=dev-1&MediaSourceId=%1&VideoCodec=hevc,h264"
                           "&AudioCodec=aac&AudioStreamIndex=%2&PlaySessionId=ps%3&ApiKey=%4")
                .arg(itemId)
                .arg(audio)
                .arg(session)
                .arg(QString::fromLatin1(Token));
        const QJsonObject source{{QStringLiteral("Id"), itemId},
                                 {QStringLiteral("SupportsTranscoding"), true},
                                 {QStringLiteral("TranscodingUrl"), url},
                                 {QStringLiteral("TranscodingSubProtocol"), QStringLiteral("hls")},
                                 {QStringLiteral("TranscodingContainer"), QStringLiteral("mp4")}};
        return {200,
                QJsonDocument(QJsonObject{{QStringLiteral("MediaSources"), QJsonArray{source}},
                                          {QStringLiteral("PlaySessionId"),
                                           QStringLiteral("ps%1").arg(session)}})
                    .toJson(QJsonDocument::Compact)};
      }
      const QString subtitles =
          QStringLiteral("/jf/Videos/%1/%1/Subtitles/").arg(itemId);
      if (r.method == "GET" && r.path.startsWith(subtitles)) {
        subtitleRequests.append(r.path.mid(subtitles.size()));
        return {200, "1\n00:00:01,000 --> 00:00:02,000\nHello\n", "text/plain"};
      }
      if (r.method != "GET" || !r.path.startsWith(videos)) return {404, {}, {}};
      const int session = sessionOf(r);
      const QString rest = r.path.mid(videos.size());
      if (rest == QLatin1String("master.m3u8"))
        return {200,
                "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=3936000,CODECS=\"hvc1.1.4.L120.B0,mp4a.40.2\"\n"
                "main.m3u8?DeviceId=dev-1&PlaySessionId=ps" +
                    QByteArray::number(session) + "\n",
                "application/vnd.apple.mpegurl"};
      if (rest == QLatin1String("main.m3u8"))
        return {200, mediaPlaylist(session), "application/vnd.apple.mpegurl"};
      if (rest == QLatin1String("hls1/main/-1.mp4")) return {200, initBytes(), "video/mp4"};
      if (rest.startsWith(QLatin1String("hls1/main/")) && rest.endsWith(QLatin1String(".mp4"))) {
        const int n = rest.mid(10).chopped(4).toInt();
        segmentRequests.append({n, session});
        log.append(itemId + QLatin1Char(' ') + QString::number(n));
        if (segmentHook)
          if (auto answer = segmentHook(n, session)) return *answer;
        if (n < 0 || n >= segmentCount) return {404, {}, {}};
        return {200, segmentData(n), "video/mp4"};
      }
      return {404, {}, {}};
    }

    FakeServer m_server;
  };

} // namespace jfd::test
