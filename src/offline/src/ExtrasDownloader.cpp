#include <jfd/offline/ExtrasDownloader.h>

#include <jfd/api/Downloads.h>
#include <jfd/offline/LibraryNaming.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

namespace jfd::offline {

  namespace {
    Q_LOGGING_CATEGORY(lcExtras, "jfd.offline.extras")

    // Card and backdrop sizes on a 2x screen; never the originals.
    struct ImageKind {
      const char* type;
      int maxWidth;
    };
    constexpr ImageKind ImageKinds[] = {
        {"Primary", 600}, {"Logo", 800}, {"Thumb", 800}, {"Backdrop", 1920}};

    constexpr int RequestTimeoutMs = 30'000;
  } // namespace

  ExtrasDownloader::ExtrasDownloader(DownloadStore& store, OfflineCatalog& catalog, QObject* parent)
      : QObject(parent), m_store(store), m_catalog(catalog),
        m_network(new QNetworkAccessManager(this)) {}

  ExtrasDownloader::~ExtrasDownloader() {
    if (m_reply) {
      m_reply->disconnect(this);
      m_reply->abort();
    }
  }

  void ExtrasDownloader::setAccess(const QString& serverId, const QString& userId,
                                   const QUrl& serverUrl, const QByteArray& authorization) {
    const bool sameUser = serverId == m_serverId && userId == m_userId;
    m_serverUrl = serverUrl;
    m_authorization = authorization;
    if (sameUser && !serverUrl.isEmpty()) return;
    m_serverId = serverId;
    m_userId = userId;
    m_pending.clear();
    if (m_reply) {
      m_reply->disconnect(this);
      m_reply->abort();
      m_reply = nullptr;
    }
    m_current.clear();
    m_tasks.clear();
  }

  void ExtrasDownloader::fetch(const QString& itemId) {
    if (itemId == m_current || m_pending.contains(itemId)) return;
    m_pending.append(itemId);
    if (m_current.isEmpty()) next();
  }

  void ExtrasDownloader::fetchMissing() {
    if (m_serverId.isEmpty() || m_userId.isEmpty()) return;
    for (const ItemManifest& m : m_catalog.items(m_serverId, m_userId))
      if (m.state == ItemState::Complete && !m.extrasComplete) fetch(m.itemId);
  }

  QList<ExtrasDownloader::Task> ExtrasDownloader::tasksFor(const ItemManifest& m) const {
    QList<Task> tasks;
    const QString dir = m_store.itemDir(m);
    const auto have = [&dir](const QString& file) {
      return !file.isEmpty() && QFileInfo::exists(dir + QLatin1Char('/') + file);
    };
    // Subtitles: beside the media in the library (format 2), named the
    // way Jellyfin and players match them to the video.
    const auto haveSidecar = [this, &m](const QString& file) {
      return !file.isEmpty() && QFileInfo::exists(m_store.filePath(m, file));
    };
    QStringList used = m.subtitleFiles.values() + m.audioFiles.values();
    const QString mediaBase =
        m.mediaFile.left(m.mediaFile.size() - QFileInfo(m.mediaFile).suffix().size() - 1);
    const bool library = m.format != 1 && !m.mediaFile.startsWith(QLatin1Char('.'));
    const QString sourceId = m.mediaSource.value(QLatin1String("Id")).toString(m.itemId);

    const QJsonArray streams = m.mediaSource.value(QLatin1String("MediaStreams")).toArray();
    for (const auto& value : streams) {
      const QJsonObject stream = value.toObject();
      if (stream.value(QLatin1String("Type")).toString() != QLatin1String("Subtitle")) continue;
      if (!stream.value(QLatin1String("IsExternal")).toBool()) continue;
      const QString format = api::textSubtitleFormat(stream.value(QLatin1String("Codec")).toString());
      if (format.isEmpty()) continue;
      const int index = stream.value(QLatin1String("Index")).toInt(-1);
      if (index < 0) continue;
      if (haveSidecar(m.subtitleFiles.value(index))) continue;
      Task task;
      task.kind = Task::Kind::Subtitle;
      task.streamIndex = index;
      if (library) {
        task.file = sidecarPath(mediaBase,
                                sidecarSuffix(stream, m.mediaSource.value(QLatin1String("Path")).toString()),
                                format, used);
        used.append(task.file);
      } else if (m.format != 1) {
        // The media is still inside the item folder (not moved yet).
        task.file = QDir(m_store.root()).relativeFilePath(m_store.itemDir(m)) +
                    QStringLiteral("/subs/%1.%2").arg(index).arg(format);
      } else {
        task.file = QStringLiteral("subs/%1.%2").arg(index).arg(format);
      }
      task.url = api::subtitleDownloadUrl(m_serverUrl, m.itemId, sourceId, index, format);
      tasks.append(task);
    }

    const QJsonObject tags = m.item.value(QLatin1String("ImageTags")).toObject();
    const QJsonArray backdrops = m.item.value(QLatin1String("BackdropImageTags")).toArray();
    for (const ImageKind& kind : ImageKinds) {
      const QString type = QLatin1String(kind.type);
      const bool backdrop = type == QLatin1String("Backdrop");
      const QString tag = backdrop ? (backdrops.isEmpty() ? QString() : backdrops.first().toString())
                                   : tags.value(type).toString();
      if (tag.isEmpty() || have(m.imageFiles.value(type))) continue;
      Task task;
      task.kind = Task::Kind::Image;
      task.imageType = type;
      task.file = QStringLiteral("images/") + type;
      task.url = api::imageDownloadUrl(m_serverUrl, m.itemId, type,
                                       backdrop ? std::optional<int>(0) : std::nullopt, tag,
                                       kind.maxWidth);
      tasks.append(task);
    }

    const QString mediaType = m.item.value(QLatin1String("MediaType")).toString();
    const QJsonValue hasLyrics = m.item.value(QLatin1String("HasLyrics"));
    if (mediaType == QLatin1String("Audio") && !(hasLyrics.isBool() && !hasLyrics.toBool()) &&
        !have(m.lyricsFile)) {
      Task task;
      task.kind = Task::Kind::Lyrics;
      task.file = QStringLiteral("lyrics.json");
      task.url = api::lyricsDownloadUrl(m_serverUrl, m.itemId);
      tasks.append(task);
    }
    if (mediaType == QLatin1String("Video") && !have(m.segmentsFile)) {
      Task task;
      task.kind = Task::Kind::Segments;
      task.file = QStringLiteral("segments.json");
      task.url = api::mediaSegmentsDownloadUrl(m_serverUrl, m.itemId);
      tasks.append(task);
    }
    return tasks;
  }

  void ExtrasDownloader::next() {
    m_current.clear();
    if (m_serverUrl.isEmpty()) return;
    while (!m_pending.isEmpty()) {
      const QString itemId = m_pending.takeFirst();
      const auto manifest = m_catalog.find(m_serverId, m_userId, itemId);
      if (!manifest || manifest->state != ItemState::Complete) continue;
      m_current = itemId;
      m_manifest = *manifest;
      m_tasks = tasksFor(m_manifest);
      m_failed = false;
      startTask();
      return;
    }
  }

  void ExtrasDownloader::startTask() {
    if (m_tasks.isEmpty()) {
      finishItem(!m_failed);
      return;
    }
    const Task& task = m_tasks.first();
    if (!api::isServerUrl(m_serverUrl, task.url)) {
      // Cannot happen with URLs built from the server address; never send
      // the token anywhere else.
      m_tasks.removeFirst();
      startTask();
      return;
    }
    QNetworkRequest request(task.url);
    request.setRawHeader("Authorization", m_authorization);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(RequestTimeoutMs);
    m_reply = m_network->get(request);
    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReply(reply); });
  }

  void ExtrasDownloader::onReply(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply != m_reply) return;
    m_reply = nullptr;
    const Task task = m_tasks.takeFirst();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!m_catalog.find(m_serverId, m_userId, m_current)) {
      // Deleted meanwhile: write nothing, or its folder comes back.
      m_tasks.clear();
      m_failed = true;
    } else if (status >= 200 && status < 300) {
      const QString path = task.kind == Task::Kind::Subtitle
                               ? m_store.filePath(m_manifest, task.file)
                               : m_store.itemDir(m_manifest) + QLatin1Char('/') + task.file;
      QDir().mkpath(QFileInfo(path).absolutePath());
      QSaveFile file(path);
      const QByteArray body = reply->readAll();
      if (file.open(QIODevice::WriteOnly) && file.write(body) == body.size() && file.commit()) {
        switch (task.kind) {
        case Task::Kind::Subtitle:
          m_manifest.subtitleFiles.insert(task.streamIndex, task.file);
          break;
        case Task::Kind::Image:
          m_manifest.imageFiles.insert(task.imageType, task.file);
          break;
        case Task::Kind::Lyrics:
          m_manifest.lyricsFile = task.file;
          break;
        case Task::Kind::Segments:
          m_manifest.segmentsFile = task.file;
          break;
        }
      } else {
        qCWarning(lcExtras) << "cannot write" << task.file << "of" << m_current;
        m_failed = true;
      }
    } else if (status == 401 || status == 0 || status >= 500) {
      // Signed out, no connection or a server error: try the rest of the
      // item at the next sign-in.
      qCInfo(lcExtras) << "extras of" << m_current << "stopped at" << task.file << "status" << status;
      m_failed = true;
      m_tasks.clear();
    }
    // Other answers (404: the server has none; 3xx: not followed) leave the
    // extra out for good.
    startTask();
  }

  void ExtrasDownloader::finishItem(bool complete) {
    const QString itemId = m_current;
    // The manifest may have changed meanwhile (user data refreshed): only
    // the extras are this class's to set.
    if (auto latest = m_catalog.find(m_serverId, m_userId, itemId)) {
      latest->subtitleFiles = m_manifest.subtitleFiles;
      latest->imageFiles = m_manifest.imageFiles;
      latest->lyricsFile = m_manifest.lyricsFile;
      latest->segmentsFile = m_manifest.segmentsFile;
      latest->extrasComplete = complete;
      if (m_store.writeManifest(*latest)) m_catalog.update(*latest);
      Q_EMIT finished(itemId, complete);
    }
    next();
  }

} // namespace jfd::offline
