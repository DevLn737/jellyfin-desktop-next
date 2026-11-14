#include <jfd/api/Downloads.h>

#include <jfd/api/JellyfinClient.h>

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
