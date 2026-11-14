#include <jfd/api/Downloads.h>

#include <QStringList>

namespace jfd::api {

  QUrl originalDownloadUrl(const QUrl& serverUrl, const QString& mediaSourceId) {
    QUrl url = serverUrl;
    QString path = serverUrl.path();
    while (path.endsWith(QLatin1Char('/')))
      path.chop(1);
    path += QStringLiteral("/Items/") + mediaSourceId + QStringLiteral("/Download");
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
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
