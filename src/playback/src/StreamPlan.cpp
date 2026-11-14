#include <jfd/playback/StreamPlan.h>

#include <QDir>
#include <QFileInfo>
#include <QUrlQuery>

namespace jfd::playback {

  namespace {
    bool copiesTimestamps(const QUrl& url) {
      const QUrlQuery query(url);
      for (const auto& [name, value] : query.queryItems())
        if (name.compare(QLatin1String("CopyTimestamps"), Qt::CaseInsensitive) == 0)
          return value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
      return false;
    }
  } // namespace

  QString windowsPlayerPath(const QString& path) {
    QString native = path;
    native.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (native.size() < 260 || native.startsWith(QLatin1String("\\\\?\\"))) return native;
    if (native.startsWith(QLatin1String("\\\\")))
      return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    return QStringLiteral("\\\\?\\") + native;
  }

  std::string playerAddress(const QUrl& url) {
    if (!url.isLocalFile()) return url.toString(QUrl::FullyEncoded).toStdString();
#ifdef Q_OS_WIN
    return windowsPlayerPath(QDir::cleanPath(QFileInfo(url.toLocalFile()).absoluteFilePath()))
        .toStdString();
#else
    return url.toLocalFile().toStdString();
#endif
  }

  core::StreamTimeline timelineFor(const api::PlaybackStream& stream, bool audioOnly,
                                   core::MediaTicks start) {
    using Kind = core::StreamTimeline::Kind;
    if (stream.method != api::PlayMethod::Transcode) return {Kind::Relative, {}};
    if (copiesTimestamps(stream.url))
      return {audioOnly ? Kind::PreservedMusic : Kind::PreservedVideo, {}};
    if (stream.isHls) return {Kind::Relative, {}};
    return {Kind::Relative, start};
  }

  core::MediaRequest mediaRequest(const api::PlaybackStream& stream, bool audioOnly,
                                  core::MediaTicks start, std::optional<core::MediaTicks> runTime,
                                  bool startPaused) {
    core::MediaRequest request;
    request.item = core::ItemId(stream.itemId.toStdString());
    request.mediaSource = core::MediaSourceId(stream.mediaSourceId.toStdString());
    request.url = playerAddress(stream.url);
    request.timeline = timelineFor(stream, audioOnly, start);
    request.start = start;
    request.runTime = runTime;
    request.startPaused = startPaused;
    return request;
  }

  api::PlaybackInfoRequest reopenRequest(const api::PlaybackInfoRequest& previous,
                                         const api::PlaybackStream& stream,
                                         core::MediaTicks position,
                                         std::optional<int> audioStreamIndex,
                                         int subtitleStreamIndex) {
    api::PlaybackInfoRequest request = previous;
    request.itemId = stream.itemId;
    request.mediaSourceId = stream.mediaSourceId;
    request.startTimeTicks = position.ticks();
    request.audioStreamIndex = audioStreamIndex;
    request.subtitleStreamIndex = subtitleStreamIndex;
    return request;
  }

} // namespace jfd::playback
