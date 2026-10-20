#include <jfd/api/Playback.h>

#include <QUrlQuery>

namespace jfd::api {

  namespace {
    const QString ApiKey = QStringLiteral("ApiKey");

    // Resolves a server-relative "/path?query" against the server URL,
    // keeping the server's own path prefix (reverse-proxy subpaths).
    QUrl resolveOnServer(const QUrl& server, const QString& relative) {
      const qsizetype queryStart = relative.indexOf(QLatin1Char('?'));
      QString path = queryStart < 0 ? relative : relative.left(queryStart);
      if (!path.startsWith(QLatin1Char('/'))) path.prepend(QLatin1Char('/'));
      QUrl url = server;
      QString prefix = server.path();
      while (prefix.endsWith(QLatin1Char('/')))
        prefix.chop(1);
      url.setPath(prefix + path);
      url.setQuery(queryStart < 0 ? QString() : relative.mid(queryStart + 1), QUrl::TolerantMode);
      url.setFragment({});
      return url;
    }

    // Adds ApiKey to a URL on our server unless it already carries a key in
    // either spelling the server accepts.
    QUrl withApiKey(QUrl url, const QString& token) {
      QUrlQuery query(url);
      if (token.isEmpty() || query.hasQueryItem(ApiKey) ||
          query.hasQueryItem(QStringLiteral("api_key")))
        return url;
      query.addQueryItem(ApiKey, token);
      url.setQuery(query);
      return url;
    }

    QUrl deliveryUrl(const StreamContext& context, const MediaStream& stream) {
      if (stream.isExternalUrl) return QUrl(stream.deliveryUrl);
      return withApiKey(resolveOnServer(context.serverUrl, stream.deliveryUrl),
                        context.accessToken);
    }

    bool isHttpUrl(const QString& path) {
      const QUrl url(path);
      return url.isValid() &&
             (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
    }

    ApiError noStream(const QString& code, const QString& message) {
      ApiError error;
      error.kind = ApiError::Kind::Unsupported;
      error.problemCode = code;
      error.message = message;
      return error;
    }

    QString percent(const QString& value) {
      return QString::fromLatin1(QUrl::toPercentEncoding(value));
    }
  } // namespace

  QJsonObject playbackInfoBody(const PlaybackInfoRequest& r) {
    QJsonObject body{{QStringLiteral("UserId"), r.userId},
                     {QStringLiteral("EnableDirectPlay"), r.enableDirectPlay},
                     {QStringLiteral("EnableDirectStream"), r.enableDirectStream},
                     {QStringLiteral("EnableTranscoding"), r.enableTranscoding},
                     {QStringLiteral("AllowVideoStreamCopy"), r.allowVideoStreamCopy},
                     {QStringLiteral("AllowAudioStreamCopy"), r.allowAudioStreamCopy},
                     {QStringLiteral("AutoOpenLiveStream"), true},
                     {QStringLiteral("DeviceProfile"), r.deviceProfile}};
    if (!r.mediaSourceId.isEmpty()) body.insert(QStringLiteral("MediaSourceId"), r.mediaSourceId);
    if (r.startTimeTicks)
      body.insert(QStringLiteral("StartTimeTicks"), static_cast<double>(*r.startTimeTicks));
    if (r.audioStreamIndex) body.insert(QStringLiteral("AudioStreamIndex"), *r.audioStreamIndex);
    if (r.subtitleStreamIndex)
      body.insert(QStringLiteral("SubtitleStreamIndex"), *r.subtitleStreamIndex);
    if (r.maxStreamingBitrate)
      body.insert(QStringLiteral("MaxStreamingBitrate"),
                  static_cast<double>(*r.maxStreamingBitrate));
    return body;
  }

  QString playMethodName(PlayMethod method) {
    switch (method) {
    case PlayMethod::DirectPlay:
      return QStringLiteral("DirectPlay");
    case PlayMethod::DirectStream:
      return QStringLiteral("DirectStream");
    case PlayMethod::Transcode:
      return QStringLiteral("Transcode");
    }
    return {};
  }

  Result<PlaybackStream> resolvePlaybackStream(const StreamContext& context, const QString& itemId,
                                               const PlaybackInfo& info,
                                               const MediaSource& source) {
    if (!info.errorCode.isEmpty())
      return noStream(info.errorCode, QStringLiteral("The server refused playback"));

    PlaybackStream stream;
    stream.itemId = itemId;
    stream.mediaSourceId = source.id;
    stream.playSessionId = info.playSessionId;

    const bool needsHeaders = !source.requiredHttpHeaders.isEmpty();
    if (source.supportsDirectPlay && source.protocol == QLatin1String("Http") && !needsHeaders &&
        isHttpUrl(source.path)) {
      // A remote source the server only points at; mpv fetches it itself.
      stream.url = QUrl(source.path);
      stream.method = PlayMethod::DirectPlay;
    } else if ((source.supportsDirectPlay || source.supportsDirectStream) &&
               source.protocol != QLatin1String("Http")) {
      QString path = QStringLiteral("/Videos/%1/stream").arg(percent(itemId));
      const QString container = source.container.section(QLatin1Char(','), 0, 0).trimmed();
      if (!container.isEmpty()) path += QLatin1Char('.') + percent(container);
      QUrl url = resolveOnServer(context.serverUrl, path);
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("Static"), QStringLiteral("true"));
      query.addQueryItem(QStringLiteral("MediaSourceId"), source.id);
      if (!context.deviceId.isEmpty())
        query.addQueryItem(QStringLiteral("DeviceId"), context.deviceId);
      if (!info.playSessionId.isEmpty())
        query.addQueryItem(QStringLiteral("PlaySessionId"), info.playSessionId);
      if (!source.eTag.isEmpty()) query.addQueryItem(QStringLiteral("Tag"), source.eTag);
      url.setQuery(query);
      stream.url = withApiKey(url, context.accessToken);
      stream.method = source.supportsDirectPlay ? PlayMethod::DirectPlay : PlayMethod::DirectStream;
    } else if (source.supportsTranscoding && !source.transcodingUrl.isEmpty()) {
      // TranscodingUrl already carries the server's ApiKey and session.
      stream.url = withApiKey(resolveOnServer(context.serverUrl, source.transcodingUrl),
                              context.accessToken);
      stream.method = PlayMethod::Transcode;
      stream.isHls = source.transcodingSubProtocol == QLatin1String("hls");
    } else if (needsHeaders) {
      return noStream(QStringLiteral("RequiredHttpHeaders"),
                      QStringLiteral("The source needs HTTP headers this client does not send"));
    } else {
      return noStream(QStringLiteral("NoCompatibleStream"),
                      QStringLiteral("The source offers no playable stream"));
    }

    for (const MediaStream& s : source.mediaStreams) {
      if (s.type == QLatin1String("Subtitle") && s.deliveryMethod == QLatin1String("External") &&
          !s.deliveryUrl.isEmpty()) {
        stream.externalSubtitles.append({s.index, deliveryUrl(context, s), s.codec, s.language,
                                         s.displayTitle.isEmpty() ? s.title : s.displayTitle,
                                         s.isDefault, s.isForced});
      } else if (s.type == QLatin1String("Audio") &&
                 (s.isExternal || s.deliveryMethod == QLatin1String("External"))) {
        QUrl url;
        if (!s.deliveryUrl.isEmpty()) {
          url = deliveryUrl(context, s);
        } else if (!s.codec.isEmpty()) {
          // The progressive audio route, as the 2.x client derives it. No
          // URL is invented when the codec is unknown.
          const QString codec = s.codec.toLower();
          url = resolveOnServer(
              context.serverUrl,
              QStringLiteral("/Audio/%1/stream.%2").arg(percent(itemId), percent(codec)));
          QUrlQuery query;
          query.addQueryItem(QStringLiteral("MediaSourceId"), source.id);
          query.addQueryItem(QStringLiteral("AudioStreamIndex"), QString::number(s.index));
          query.addQueryItem(QStringLiteral("AudioCodec"), codec);
          query.addQueryItem(QStringLiteral("TranscodingProtocol"), QStringLiteral("http"));
          query.addQueryItem(QStringLiteral("EnableRedirection"), QStringLiteral("false"));
          if (!context.deviceId.isEmpty())
            query.addQueryItem(QStringLiteral("DeviceId"), context.deviceId);
          if (!info.playSessionId.isEmpty())
            query.addQueryItem(QStringLiteral("PlaySessionId"), info.playSessionId);
          url.setQuery(query);
          url = withApiKey(url, context.accessToken);
        }
        if (url.isValid() && !url.isEmpty())
          stream.externalAudio.append({s.index, url, s.codec, s.language,
                                       s.displayTitle.isEmpty() ? s.title : s.displayTitle,
                                       s.isDefault, s.isForced});
      }
    }
    return stream;
  }

  std::optional<int> embeddedTrackId(const MediaSource& source, int streamIndex) {
    QString type;
    for (const MediaStream& s : source.mediaStreams) {
      if (s.index == streamIndex) {
        if (s.isExternal) return std::nullopt;
        type = s.type;
        break;
      }
    }
    if (type.isEmpty()) return std::nullopt;
    int trackId = 1;
    for (const MediaStream& s : source.mediaStreams) {
      if (s.type != type || s.isExternal) continue;
      if (s.index == streamIndex) return trackId;
      ++trackId;
    }
    return std::nullopt;
  }

  namespace {
    QJsonObject progressBody(const PlaybackProgress& p) {
      QJsonObject body{{QStringLiteral("ItemId"), p.itemId},
                       {QStringLiteral("MediaSourceId"), p.mediaSourceId},
                       {QStringLiteral("PlaySessionId"), p.playSessionId},
                       {QStringLiteral("PositionTicks"), static_cast<double>(p.positionTicks)},
                       {QStringLiteral("IsPaused"), p.isPaused},
                       {QStringLiteral("IsMuted"), p.isMuted},
                       {QStringLiteral("VolumeLevel"), p.volumeLevel},
                       {QStringLiteral("PlayMethod"), playMethodName(p.playMethod)},
                       {QStringLiteral("PlaybackRate"), p.playbackRate},
                       {QStringLiteral("CanSeek"), true}};
      if (p.audioStreamIndex) body.insert(QStringLiteral("AudioStreamIndex"), *p.audioStreamIndex);
      if (p.subtitleStreamIndex)
        body.insert(QStringLiteral("SubtitleStreamIndex"), *p.subtitleStreamIndex);
      if (!p.eventName.isEmpty()) body.insert(QStringLiteral("EventName"), p.eventName);
      return body;
    }
  } // namespace

  QJsonObject playbackStartBody(const PlaybackProgress& progress) {
    return progressBody(progress);
  }

  QJsonObject playbackProgressBody(const PlaybackProgress& progress) {
    return progressBody(progress);
  }

  QJsonObject playbackStoppedBody(const PlaybackProgress& p, bool failed) {
    return QJsonObject{{QStringLiteral("ItemId"), p.itemId},
                       {QStringLiteral("MediaSourceId"), p.mediaSourceId},
                       {QStringLiteral("PlaySessionId"), p.playSessionId},
                       {QStringLiteral("PositionTicks"), static_cast<double>(p.positionTicks)},
                       {QStringLiteral("Failed"), failed}};
  }

} // namespace jfd::api
