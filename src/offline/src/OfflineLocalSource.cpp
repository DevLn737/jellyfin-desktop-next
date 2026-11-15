#include <jfd/offline/OfflineLocalSource.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace jfd::offline {

  namespace {
    std::optional<QJsonObject> readObject(const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
      const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
      if (!document.isObject()) return std::nullopt;
      return document.object();
    }
  } // namespace

  std::optional<session::LocalCopy> OfflineLocalSource::copy(const QString& itemId) const {
    if (m_serverId.isEmpty() || m_userId.isEmpty()) return std::nullopt;
    const auto manifest = m_catalog.find(m_serverId, m_userId, itemId);
    if (!manifest || manifest->state != ItemState::Complete) return std::nullopt;
    const QString path = m_store.mediaPath(*manifest);
    if (path.isEmpty() || !QFileInfo(path).isFile()) return std::nullopt;
    auto item = api::json::item(manifest->item);
    auto source = api::json::mediaSource(manifest->mediaSource);
    if (!item || !source) return std::nullopt;

    session::LocalCopy copy;
    copy.item = std::move(item).value();
    copy.source = std::move(source).value();
    copy.path = path;
    copy.original = manifest->quality == QLatin1String("original");
    const QString dir = m_store.itemDir(*manifest);
    for (api::MediaStream& stream : copy.source.mediaStreams) {
      // The file has every track it was made with. How the server would
      // have delivered one to a stream (extracted, burned in) does not apply.
      if (!stream.isExternal) {
        stream.deliveryMethod.clear();
        stream.deliveryUrl.clear();
        continue;
      }
      const QString file = manifest->subtitleFiles.value(stream.index);
      if (stream.type != QLatin1String("Subtitle") || file.isEmpty()) continue;
      const QString subtitlePath = dir + QLatin1Char('/') + file;
      if (!QFileInfo(subtitlePath).isFile()) continue;
      copy.externalSubtitles.append({stream.index, QUrl::fromLocalFile(subtitlePath), stream.codec,
                                     stream.language,
                                     stream.displayTitle.isEmpty() ? stream.title : stream.displayTitle,
                                     stream.isDefault, stream.isForced});
    }
    if (!manifest->segmentsFile.isEmpty())
      if (const auto body = readObject(dir + QLatin1Char('/') + manifest->segmentsFile))
        if (auto segments = api::json::mediaSegments(*body)) copy.segments = std::move(segments).value();
    return copy;
  }

  std::optional<session::LocalCopy> OfflineLocalSource::find(const QString& itemId) const {
    if (m_online && !m_preferLocal) return std::nullopt;
    return copy(itemId);
  }

  std::optional<api::Lyrics> OfflineLocalSource::lyrics(const QString& itemId) const {
    if (m_serverId.isEmpty() || m_userId.isEmpty()) return std::nullopt;
    const auto manifest = m_catalog.find(m_serverId, m_userId, itemId);
    if (!manifest || manifest->state != ItemState::Complete) return std::nullopt;
    if (manifest->lyricsFile.isEmpty()) {
      // Not fetched yet: the server may still have them.
      if (!manifest->extrasComplete) return std::nullopt;
      return api::Lyrics{};
    }
    const auto body = readObject(m_store.itemDir(*manifest) + QLatin1Char('/') + manifest->lyricsFile);
    if (!body) return std::nullopt;
    return api::Lyrics::fromJson(*body);
  }

} // namespace jfd::offline
