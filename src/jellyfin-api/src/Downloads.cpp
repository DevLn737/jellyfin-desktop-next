#include <jfd/api/Downloads.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/Playback.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QUrlQuery>

namespace jfd::api {

  namespace {
    // `relative` under the server's base path, with `query`.
    QUrl serverPath(const QUrl& serverUrl, const QString& relative, const QUrlQuery& query = {}) {
      QUrl url = serverUrl;
      QString path = serverUrl.path();
      while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
      url.setPath(path + QLatin1Char('/') + relative);
      url.setQuery(query);
      url.setFragment(QString());
      return url;
    }

    QUrlQuery userQuery(const JellyfinClient& client) {
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("userId"), client.userId());
      return query;
    }
  } // namespace

  QUrl originalDownloadUrl(const QUrl& serverUrl, const QString& mediaSourceId) {
    return serverPath(serverUrl, QStringLiteral("Items/") + mediaSourceId + QStringLiteral("/Download"));
  }

  QJsonObject compressedDeviceProfile(const CompressedProfile& profile) {
    const double bitrate = static_cast<double>(profile.bitrate);
    QJsonObject transcoding{{QStringLiteral("Context"), QStringLiteral("Streaming")},
                            {QStringLiteral("Protocol"), QStringLiteral("hls")},
                            {QStringLiteral("Container"), QStringLiteral("mp4")},
                            {QStringLiteral("MaxAudioChannels"), QStringLiteral("2")},
                            {QStringLiteral("SegmentLength"), 6},
                            {QStringLiteral("BreakOnNonKeyFrames"), false}};
    QJsonArray codecProfiles;
    if (profile.audio) {
      transcoding.insert(QStringLiteral("Type"), QStringLiteral("Audio"));
      transcoding.insert(QStringLiteral("AudioCodec"), QStringLiteral("opus"));
    } else {
      transcoding.insert(QStringLiteral("Type"), QStringLiteral("Video"));
      transcoding.insert(QStringLiteral("VideoCodec"), QStringLiteral("hevc,h264"));
      transcoding.insert(QStringLiteral("AudioCodec"), QStringLiteral("aac"));
      if (profile.maxHeight > 0) {
        const QString height = QString::number(profile.maxHeight);
        // 16:9 at that height; wider films are limited by their width.
        const QString width = QString::number(profile.maxHeight * 16 / 9);
        for (const char* codec : {"hevc", "h264"}) {
          codecProfiles.append(QJsonObject{
              {QStringLiteral("Type"), QStringLiteral("Video")},
              {QStringLiteral("Codec"), QLatin1String(codec)},
              {QStringLiteral("Conditions"),
               QJsonArray{QJsonObject{{QStringLiteral("Condition"), QStringLiteral("LessThanEqual")},
                                      {QStringLiteral("Property"), QStringLiteral("Height")},
                                      {QStringLiteral("Value"), height},
                                      {QStringLiteral("IsRequired"), true}},
                          QJsonObject{{QStringLiteral("Condition"), QStringLiteral("LessThanEqual")},
                                      {QStringLiteral("Property"), QStringLiteral("Width")},
                                      {QStringLiteral("Value"), width},
                                      {QStringLiteral("IsRequired"), true}}}}});
        }
      }
    }
    QJsonArray subtitles;
    for (const char* format : {"srt", "ass", "ssa", "vtt"})
      subtitles.append(QJsonObject{{QStringLiteral("Format"), QLatin1String(format)},
                                   {QStringLiteral("Method"), QStringLiteral("External")}});
    return QJsonObject{{QStringLiteral("Name"), QStringLiteral("Jellyfin Desktop Next download")},
                       {QStringLiteral("MaxStreamingBitrate"), bitrate},
                       {QStringLiteral("MaxStaticBitrate"), bitrate},
                       {QStringLiteral("MusicStreamingTranscodingBitrate"), bitrate},
                       {QStringLiteral("DirectPlayProfiles"), QJsonArray{}},
                       {QStringLiteral("TranscodingProfiles"), QJsonArray{transcoding}},
                       {QStringLiteral("ContainerProfiles"), QJsonArray{}},
                       {QStringLiteral("CodecProfiles"), codecProfiles},
                       {QStringLiteral("SubtitleProfiles"), subtitles}};
  }

  QJsonObject compressedPlaybackInfoBody(const QString& userId, const QString& mediaSourceId,
                                         const CompressedProfile& profile,
                                         std::optional<int> audioStreamIndex) {
    PlaybackInfoRequest request;
    request.userId = userId;
    request.mediaSourceId = mediaSourceId;
    request.audioStreamIndex = audioStreamIndex;
    request.subtitleStreamIndex = -1;
    request.maxStreamingBitrate = profile.bitrate;
    request.enableDirectPlay = false;
    request.enableDirectStream = false;
    request.enableTranscoding = true;
    // Always encoded: copied video keeps its own key frames, and the
    // segments would not match the playlist's durations.
    request.allowVideoStreamCopy = false;
    request.allowAudioStreamCopy = false;
    request.deviceProfile = compressedDeviceProfile(profile);
    QJsonObject body = playbackInfoBody(request);
    // Not a live stream to open; the server's answer is all that is needed.
    body.insert(QStringLiteral("AutoOpenLiveStream"), false);
    return body;
  }

  QUrl playbackInfoUrl(const QUrl& serverUrl, const QString& itemId, const QString& userId) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), userId);
    return serverPath(serverUrl, QStringLiteral("Items/") + itemId + QStringLiteral("/PlaybackInfo"),
                      query);
  }

  QUrl transcodingUrlOnServer(const QUrl& serverUrl, const QString& transcodingUrl) {
    const QUrl relative(transcodingUrl);
    if (!relative.isValid() || transcodingUrl.isEmpty()) return {};
    QUrl url;
    if (relative.isRelative()) {
      QString path = relative.path();
      while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
      url = serverPath(serverUrl, path);
    } else {
      // An absolute URL is used only when it is the server's own.
      if (!isServerUrl(serverUrl, relative)) return {};
      url = relative;
      url.setFragment(QString());
    }
    QUrlQuery query(relative.query(QUrl::FullyEncoded));
    for (const auto& [key, value] : query.queryItems(QUrl::FullyEncoded)) {
      const QString k = key.toLower();
      if (k == QLatin1String("apikey") || k == QLatin1String("api_key"))
        query.removeAllQueryItems(key);
    }
    url.setQuery(query);
    return url;
  }

  QUrl activeEncodingsUrl(const QUrl& serverUrl, const QString& deviceId,
                          const QString& playSessionId) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("deviceId"), deviceId);
    query.addQueryItem(QStringLiteral("playSessionId"), playSessionId);
    return serverPath(serverUrl, QStringLiteral("Videos/ActiveEncodings"), query);
  }

  std::int64_t estimatedCompressedSize(const CompressedProfile& profile,
                                       std::int64_t runTimeTicks) {
    if (runTimeTicks <= 0 || profile.bitrate <= 0) return 0;
    // bits/s * seconds / 8, in integers (ticks are 100 ns).
    return profile.bitrate / 8 * runTimeTicks / 10'000'000;
  }

  QUrl subtitleDownloadUrl(const QUrl& serverUrl, const QString& itemId,
                           const QString& mediaSourceId, int streamIndex, const QString& format) {
    return serverPath(serverUrl, QStringLiteral("Videos/%1/%2/Subtitles/%3/Stream.%4")
                                     .arg(itemId, mediaSourceId, QString::number(streamIndex), format));
  }

  QString textSubtitleFormat(const QString& codec) {
    const QString c = codec.toLower();
    if (c == QLatin1String("subrip") || c == QLatin1String("srt")) return QStringLiteral("srt");
    if (c == QLatin1String("ass")) return QStringLiteral("ass");
    if (c == QLatin1String("ssa")) return QStringLiteral("ssa");
    if (c == QLatin1String("webvtt") || c == QLatin1String("vtt")) return QStringLiteral("vtt");
    return {};
  }

  QUrl imageDownloadUrl(const QUrl& serverUrl, const QString& itemId, const QString& type,
                        std::optional<int> index, const QString& tag, int maxWidth) {
    QUrlQuery query;
    if (!tag.isEmpty()) query.addQueryItem(QStringLiteral("tag"), tag);
    if (maxWidth > 0) query.addQueryItem(QStringLiteral("maxWidth"), QString::number(maxWidth));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
    QString path = QStringLiteral("Items/%1/Images/%2").arg(itemId, type);
    if (index) path += QLatin1Char('/') + QString::number(*index);
    return serverPath(serverUrl, path, query);
  }

  QUrl lyricsDownloadUrl(const QUrl& serverUrl, const QString& itemId) {
    return serverPath(serverUrl, QStringLiteral("Audio/") + itemId + QStringLiteral("/Lyrics"));
  }

  QUrl mediaSegmentsDownloadUrl(const QUrl& serverUrl, const QString& itemId) {
    return serverPath(serverUrl, QStringLiteral("MediaSegments/") + itemId);
  }

  void fetchUserItemData(JellyfinClient& client, const QString& itemId, QObject* context,
                         std::function<void(Result<QJsonObject>)> done) {
    client.sendJson(JellyfinClient::Method::Get,
                    QStringLiteral("UserItems/") + itemId + QStringLiteral("/UserData"),
                    userQuery(client), std::nullopt, context, std::move(done));
  }

  void updateUserItemData(JellyfinClient& client, const QString& itemId, const QJsonObject& body,
                          QObject* context, std::function<void(Result<QJsonObject>)> done) {
    client.sendJson(JellyfinClient::Method::Post,
                    QStringLiteral("UserItems/") + itemId + QStringLiteral("/UserData"),
                    userQuery(client), QJsonDocument(body), context, std::move(done));
  }

  void fetchItemsById(JellyfinClient& client, const QStringList& ids, QObject* context,
                      std::function<void(Result<QJsonArray>)> done) {
    QUrlQuery query = userQuery(client);
    query.addQueryItem(QStringLiteral("ids"), ids.join(QLatin1Char(',')));
    query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("MediaSources"));
    query.addQueryItem(QStringLiteral("enableUserData"), QStringLiteral("true"));
    client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt,
                    context, [done = std::move(done)](Result<QJsonObject> result) {
                      if (!result) {
                        done(result.error());
                        return;
                      }
                      done(result->value(QLatin1String("Items")).toArray());
                    });
  }

  void fetchDownloadableChildren(JellyfinClient& client, const QString& parentId, bool keepOrder,
                                 bool unplayedOnly, QObject* context,
                                 std::function<void(Result<QJsonArray>)> done) {
    QUrlQuery query = userQuery(client);
    query.addQueryItem(QStringLiteral("ParentId"), parentId);
    query.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("IncludeItemTypes"),
                       QStringLiteral("Movie,Episode,Video,MusicVideo,Audio,AudioBook"));
    query.addQueryItem(QStringLiteral("Fields"), QStringLiteral("MediaSources"));
    query.addQueryItem(QStringLiteral("enableUserData"), QStringLiteral("true"));
    if (!keepOrder)
      query.addQueryItem(QStringLiteral("SortBy"),
                         QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
    if (unplayedOnly) query.addQueryItem(QStringLiteral("Filters"), QStringLiteral("IsUnplayed"));
    client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt,
                    context, [done = std::move(done)](Result<QJsonObject> result) {
                      if (!result) {
                        done(result.error());
                        return;
                      }
                      done(result->value(QLatin1String("Items")).toArray());
                    });
  }

  bool isServerUrl(const QUrl& serverUrl, const QUrl& target) {
    return target.isValid() && serverUrl.isValid() &&
           target.scheme().compare(serverUrl.scheme(), Qt::CaseInsensitive) == 0 &&
           target.host().compare(serverUrl.host(), Qt::CaseInsensitive) == 0 &&
           target.port(target.scheme() == QLatin1String("https") ? 443 : 80) ==
               serverUrl.port(serverUrl.scheme() == QLatin1String("https") ? 443 : 80) &&
           target.userInfo().isEmpty();
  }

  DownloadQuality defaultDownloadQuality(const UserInfo& user, bool video) {
    if (user.enableContentDownloading) return DownloadQuality::Original;
    const bool transcoding =
        video ? user.enableVideoPlaybackTranscoding : user.enableAudioPlaybackTranscoding;
    return transcoding ? DownloadQuality::Compressed : DownloadQuality::None;
  }

  bool originalAvailable(const QJsonObject& item, const QJsonObject& source) {
    const auto text = [](const QJsonObject& o, const char* key) {
      return o.value(QLatin1String(key)).toString();
    };
    if (item.value(QLatin1String("IsFolder")).toBool()) return false;
    const QString videoType = text(item, "VideoType");
    if (!videoType.isEmpty() && videoType != QLatin1String("VideoFile")) return false;
    const QString sourceVideoType = text(source, "VideoType");
    if (!sourceVideoType.isEmpty() && sourceVideoType != QLatin1String("VideoFile")) return false;
    if (source.value(QLatin1String("IsRemote")).toBool()) return false;
    const QString protocol = text(source, "Protocol");
    if (!protocol.isEmpty() && protocol != QLatin1String("File")) return false;
    const QString container = text(source, "Container");
    if (container.compare(QLatin1String("strm"), Qt::CaseInsensitive) == 0) return false;
    if (text(source, "Path").endsWith(QLatin1String(".strm"), Qt::CaseInsensitive)) return false;
    return true;
  }

} // namespace jfd::api
