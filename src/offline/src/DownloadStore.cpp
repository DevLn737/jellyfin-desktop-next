#include <jfd/offline/DownloadStore.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace jfd::offline {

  namespace {
    const QString ManifestName = QStringLiteral("item.json");
    const QString PartName = QStringLiteral("media.part");
    // Parents of items (series, season, album) are kept beside the user's
    // items from stage 3.10a step 4 on; the scan skips their folder.
    const QString ParentsName = QStringLiteral("_parents");
  } // namespace

  DownloadStore::DownloadStore(QString root) : m_root(QDir::cleanPath(std::move(root))) {}

  bool DownloadStore::isSafeId(const QString& id) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{0,63}$"));
    return pattern.match(id).hasMatch();
  }

  QString DownloadStore::itemDir(const QString& serverId, const QString& userId,
                                 const QString& itemId) const {
    return m_root + QLatin1Char('/') + serverId + QLatin1Char('/') + userId + QLatin1Char('/') +
           itemId;
  }

  QString DownloadStore::itemDir(const ItemManifest& m) const {
    return itemDir(m.serverId, m.userId, m.itemId);
  }

  QString DownloadStore::manifestPath(const ItemManifest& m) const {
    return itemDir(m) + QLatin1Char('/') + ManifestName;
  }

  QString DownloadStore::partPath(const ItemManifest& m) const {
    return itemDir(m) + QLatin1Char('/') + PartName;
  }

  QString DownloadStore::mediaPath(const ItemManifest& m) const {
    if (m.mediaFile.isEmpty()) return {};
    return itemDir(m) + QLatin1Char('/') + m.mediaFile;
  }

  bool DownloadStore::writeManifest(const ItemManifest& m) const {
    if (!isSafeId(m.serverId) || !isSafeId(m.userId) || !isSafeId(m.itemId)) return false;
    if (!QDir().mkpath(itemDir(m))) return false;
    QSaveFile file(manifestPath(m));
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray data = QJsonDocument(toJson(m)).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size()) {
      file.cancelWriting();
      return false;
    }
    return file.commit();
  }

  namespace {
    std::optional<ItemManifest> readFile(const QString& path) {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
      QJsonParseError error{};
      const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
      if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
      return manifestFromJson(document.object());
    }
  } // namespace

  std::optional<ItemManifest> DownloadStore::readManifest(const QString& serverId,
                                                          const QString& userId,
                                                          const QString& itemId) const {
    if (!isSafeId(serverId) || !isSafeId(userId) || !isSafeId(itemId)) return std::nullopt;
    auto m = readFile(itemDir(serverId, userId, itemId) + QLatin1Char('/') + ManifestName);
    // A manifest copied into another item's folder is not that item.
    if (m && (m->serverId != serverId || m->userId != userId || m->itemId != itemId))
      return std::nullopt;
    return m;
  }

  QList<ItemManifest> DownloadStore::scan() const {
    QList<ItemManifest> out;
    const QDir::Filters dirs = QDir::Dirs | QDir::NoDotAndDotDot;
    const QDir root(m_root);
    for (const QString& server : root.entryList(dirs)) {
      if (!isSafeId(server)) continue;
      const QDir serverDir(root.filePath(server));
      for (const QString& user : serverDir.entryList(dirs)) {
        if (!isSafeId(user)) continue;
        const QDir userDir(serverDir.filePath(user));
        for (const QString& item : userDir.entryList(dirs, QDir::Unsorted)) {
          if (item == ParentsName || !isSafeId(item)) continue;
          auto m = readFile(userDir.filePath(item) + QLatin1Char('/') + ManifestName);
          if (m && m->serverId == server && m->userId == user && m->itemId == item)
            out.append(std::move(*m));
        }
      }
    }
    return out;
  }

  bool DownloadStore::removeItem(const ItemManifest& m) const {
    if (!isSafeId(m.serverId) || !isSafeId(m.userId) || !isSafeId(m.itemId)) return false;
    QDir dir(itemDir(m));
    if (!dir.exists()) return true;
    return dir.removeRecursively();
  }

  QString DownloadStore::windowsExternalPath(const QString& path) {
    QString native = path;
    native.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (native.size() < 260 || native.startsWith(QLatin1String("\\\\?\\"))) return native;
    if (native.startsWith(QLatin1String("\\\\")))
      return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    return QStringLiteral("\\\\?\\") + native;
  }

  QString DownloadStore::externalPath(const QString& path) {
#ifdef Q_OS_WIN
    return windowsExternalPath(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
#else
    return path;
#endif
  }

} // namespace jfd::offline
