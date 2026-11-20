#include <jfd/offline/DownloadStore.h>

#include <jfd/offline/LibraryNaming.h>
#include <jfd/playback/StreamPlan.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QLoggingCategory>
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace jfd::offline {

  namespace {
    const QString ManifestName = QStringLiteral("item.json");
    const QString PartName = QStringLiteral("media.part");
    // Parents of items (series, season, album) are kept beside the user's
    // items from stage 3.10a step 4 on; the scan skips their folder.
    const QString ParentsName = QStringLiteral("_parents");
    const QString InternalName = QStringLiteral(".jellyfin-desktop");

    Q_LOGGING_CATEGORY(lcStore, "jfd.offline.store")

    // Hidden on Windows too (a leading dot hides it elsewhere).
    void hide(const QString& path) {
#ifdef Q_OS_WIN
      const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
      const DWORD attributes = GetFileAttributesW(native.c_str());
      if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_HIDDEN))
        SetFileAttributesW(native.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
#else
      Q_UNUSED(path)
#endif
    }

    // Moves a file, making its folder; false leaves it where it was.
    bool moveFile(const QString& from, const QString& to) {
      if (!QDir().mkpath(QFileInfo(to).path())) return false;
      return QFile::rename(from, to);
    }
  } // namespace

  DownloadStore::DownloadStore(QString root) : m_root(QDir::cleanPath(std::move(root))) {}

  bool DownloadStore::isSafeId(const QString& id) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{0,63}$"));
    return pattern.match(id).hasMatch();
  }

  QString DownloadStore::internalRoot() const { return m_root + QLatin1Char('/') + InternalName; }

  QString DownloadStore::itemDir(const QString& serverId, const QString& userId,
                                 const QString& itemId) const {
    return internalRoot() + QLatin1Char('/') + serverId + QLatin1Char('/') + userId + QLatin1Char('/') +
           itemId;
  }

  QString DownloadStore::legacyItemDir(const QString& serverId, const QString& userId,
                                       const QString& itemId) const {
    return m_root + QLatin1Char('/') + serverId + QLatin1Char('/') + userId + QLatin1Char('/') +
           itemId;
  }

  QString DownloadStore::itemDir(const ItemManifest& m) const {
    return m.format == 1 ? legacyItemDir(m.serverId, m.userId, m.itemId)
                         : itemDir(m.serverId, m.userId, m.itemId);
  }

  QString DownloadStore::manifestPath(const ItemManifest& m) const {
    return itemDir(m) + QLatin1Char('/') + ManifestName;
  }

  QString DownloadStore::partPath(const ItemManifest& m) const {
    return itemDir(m) + QLatin1Char('/') + PartName;
  }

  QString DownloadStore::mediaPath(const ItemManifest& m) const {
    if (m.mediaFile.isEmpty()) return {};
    return filePath(m, m.mediaFile);
  }

  QString DownloadStore::filePath(const ItemManifest& m, const QString& file) const {
    if (file.isEmpty()) return {};
    return (m.format == 1 ? itemDir(m) : m_root) + QLatin1Char('/') + file;
  }

  QString DownloadStore::libraryFileFor(const QJsonObject& item, const QString& itemId,
                                        const QString& extension, const QSet<QString>& taken) const {
    const QString base = libraryBase(item);
    QString file = base + QLatin1Char('.') + extension;
    if (taken.contains(file.toLower()) || QFileInfo::exists(m_root + QLatin1Char('/') + file))
      file = base + QStringLiteral(" [") + itemId.left(8) + QStringLiteral("].") + extension;
    return file;
  }

  bool DownloadStore::writeManifest(const ItemManifest& m) const {
    if (!isSafeId(m.serverId) || !isSafeId(m.userId) || !isSafeId(m.itemId)) return false;
    if (m.format != 1 && !QFileInfo::exists(internalRoot())) {
      if (!QDir().mkpath(internalRoot())) return false;
      hide(internalRoot());
    }
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
    if (m && m->format == 1) m.reset();
    if (!m) {
      m = readFile(legacyItemDir(serverId, userId, itemId) + QLatin1Char('/') + ManifestName);
      if (m && m->format != 1) m.reset();
    }
    // A manifest copied into another item's folder is not that item.
    if (m && (m->serverId != serverId || m->userId != userId || m->itemId != itemId))
      return std::nullopt;
    return m;
  }

  QList<ItemManifest> DownloadStore::scan() const {
    QList<ItemManifest> out;
    QSet<QString> seen;
    const QDir::Filters dirs = QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden;
    // Format 2 first: an item found in both trees (a migration cut short)
    // is the format 2 one.
    for (const bool legacy : {false, true}) {
      const QDir root(legacy ? m_root : internalRoot());
      for (const QString& server : root.entryList(dirs)) {
        if (!isSafeId(server)) continue;
        const QDir serverDir(root.filePath(server));
        for (const QString& user : serverDir.entryList(dirs)) {
          if (!isSafeId(user)) continue;
          const QDir userDir(serverDir.filePath(user));
          for (const QString& item : userDir.entryList(dirs, QDir::Unsorted)) {
            if (item == ParentsName || !isSafeId(item)) continue;
            auto m = readFile(userDir.filePath(item) + QLatin1Char('/') + ManifestName);
            if (!m || m->serverId != server || m->userId != user || m->itemId != item) continue;
            if ((m->format == 1) != legacy) continue;
            const QString key = server + QLatin1Char('/') + user + QLatin1Char('/') + item;
            if (seen.contains(key)) continue;
            seen.insert(key);
            out.append(std::move(*m));
          }
        }
      }
    }
    return out;
  }

  std::optional<ItemManifest> DownloadStore::migrate(const ItemManifest& old, QSet<QString>& taken) const {
    if (old.format != 1) return old;
    if (!isSafeId(old.serverId) || !isSafeId(old.userId) || !isSafeId(old.itemId)) return std::nullopt;
    const QString from = legacyItemDir(old.serverId, old.userId, old.itemId);
    const QString to = itemDir(old.serverId, old.userId, old.itemId);
    if (QFileInfo::exists(to)) {
      // A move cut short before: the new folder has no manifest yet.
      if (!QDir(to).removeRecursively()) return std::nullopt;
    }
    if (!QDir().mkpath(QFileInfo(to).path())) return std::nullopt;
    hide(internalRoot());
    if (!QDir().rename(from, to)) {
      qCInfo(lcStore) << "cannot move" << old.itemId << "to the new layout; trying at the next start";
      return std::nullopt;
    }
    ItemManifest m = old;
    m.format = ItemManifest::FormatVersion;
    // Until moved, the files are where they were, inside the item folder.
    const QString inside = QDir(m_root).relativeFilePath(to) + QLatin1Char('/');
    if (!m.mediaFile.isEmpty()) m.mediaFile = inside + m.mediaFile;
    for (auto it = m.subtitleFiles.begin(); it != m.subtitleFiles.end(); ++it) it.value() = inside + it.value();

    if (m.state == ItemState::Complete && !m.mediaFile.isEmpty()) {
      m.mediaComplete = true;
      const QString extension = QFileInfo(m.mediaFile).suffix();
      const QString library = libraryFileFor(m.item, m.itemId, extension.isEmpty() ? QStringLiteral("bin") : extension, taken);
      if (moveFile(filePath(m, m.mediaFile), filePath(m, library))) {
        m.mediaFile = library;
        taken.insert(library.toLower());
        const QString base = library.left(library.size() - extension.size() - 1);
        const QString sourcePath = m.mediaSource.value(QLatin1String("Path")).toString();
        QStringList used;
        for (auto it = m.subtitleFiles.begin(); it != m.subtitleFiles.end(); ++it) {
          QJsonObject stream;
          for (const QJsonValue& v : m.mediaSource.value(QLatin1String("MediaStreams")).toArray())
            if (v.toObject().value(QLatin1String("Index")).toInt(-1) == it.key()) stream = v.toObject();
          const QString target = sidecarPath(base, sidecarSuffix(stream, sourcePath),
                                             QFileInfo(it.value()).suffix(), used);
          if (moveFile(filePath(m, it.value()), filePath(m, target))) {
            it.value() = target;
            used.append(target);
          }
        }
      } else {
        qCInfo(lcStore) << "the media of" << m.itemId << "stays in its item folder for now";
      }
    } else if (!m.mediaFile.isEmpty()) {
      // Not complete: it is renamed into the library when it is.
      const QString extension = QFileInfo(m.mediaFile).suffix();
      m.mediaFile = libraryFileFor(m.item, m.itemId, extension.isEmpty() ? QStringLiteral("bin") : extension, taken);
      taken.insert(m.mediaFile.toLower());
    }
    if (!writeManifest(m)) {
      qCWarning(lcStore) << "cannot write the migrated manifest of" << m.itemId;
      return std::nullopt;
    }
    // The old manifest went with the folder; remove empty user and server
    // folders left behind.
    QDir().rmdir(QFileInfo(from).path());
    QDir().rmdir(QFileInfo(QFileInfo(from).path()).path());
    return m;
  }

  bool DownloadStore::removeItem(const ItemManifest& m) const {
    if (!isSafeId(m.serverId) || !isSafeId(m.userId) || !isSafeId(m.itemId)) return false;
    if (m.format != 1) {
      QStringList files{m.mediaFile};
      files += m.subtitleFiles.values();
      files += m.audioFiles.values();
      // The media first: if a player holds it, nothing else goes.
      bool first = true;
      QSet<QString> folders;
      for (const QString& file : std::as_const(files)) {
        if (file.isEmpty()) continue;
        const QString path = filePath(m, file);
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
          if (first) return false;
          continue;
        }
        first = false;
        folders.insert(QFileInfo(path).path());
      }
      // Library folders left empty (season, series, type), up to the root.
      const QString root = QDir::cleanPath(m_root);
      for (QString folder : std::as_const(folders)) {
        for (int i = 0; i < 8; ++i) {
          folder = QDir::cleanPath(folder);
          if (folder.size() <= root.size() || !folder.startsWith(root + QLatin1Char('/'))) break;
          if (!QDir(folder).isEmpty() || !QDir().rmdir(folder)) break;
          folder = QFileInfo(folder).path();
        }
      }
    }
    QDir dir(itemDir(m));
    if (!dir.exists()) return true;
    return dir.removeRecursively();
  }

  QString DownloadStore::windowsExternalPath(const QString& path) {
    // One rule with what the player is given (playback::playerAddress).
    return playback::windowsPlayerPath(path);
  }

  QString DownloadStore::externalPath(const QString& path) {
#ifdef Q_OS_WIN
    return windowsExternalPath(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
#else
    return path;
#endif
  }

} // namespace jfd::offline
