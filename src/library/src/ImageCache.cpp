#include <jfd/library/ImageCache.h>

#include <jfd/api/JellyfinClient.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>

#include <algorithm>

namespace jfd::library {

  namespace {
    // Decodes straight to the target size where the format allows it (JPEG
    // scales while decoding), so a 4K backdrop asked for as a 400 px card
    // never exists in memory at full size.
    QImage decode(const QByteArray& encoded, QSize size) {
      QBuffer buffer;
      buffer.setData(encoded);
      buffer.open(QIODevice::ReadOnly);
      QImageReader reader(&buffer);
      reader.setAutoTransform(true);
      const QSize original = reader.size();
      if (size.isValid() && !size.isEmpty() && original.isValid() &&
          (original.width() > size.width() || original.height() > size.height()))
        reader.setScaledSize(original.scaled(size, Qt::KeepAspectRatio));
      QImage image = reader.read();
      if (image.isNull()) return image;
      if (size.isValid() && !size.isEmpty() &&
          (image.width() > size.width() || image.height() > size.height()))
        image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      return image;
    }

    qint64 costOf(const QImage& image) {
      return std::max<qint64>(image.sizeInBytes(), 1);
    }
  } // namespace

  ImageCache::ImageCache(api::JellyfinClient& client, const QString& diskDirectory, Limits limits,
                         QObject* parent)
      : QObject(parent), m_client(client), m_network(new QNetworkAccessManager(this)),
        m_limits(limits) {
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_memory.setMaxCost(m_limits.memoryBytes);
    if (!diskDirectory.isEmpty() && QDir().mkpath(diskDirectory)) {
      m_disk = QDir(diskDirectory);
      m_hasDisk = true;
      for (const QFileInfo& file : m_disk.entryInfoList(QDir::Files))
        m_diskBytes += file.size();
      trimDisk();
    }
  }

  ImageCache::~ImageCache() {
    for (const QString& key : m_running) {
      QNetworkReply* reply = m_fetches.value(key).reply;
      if (reply) {
        reply->disconnect(this);
        reply->abort();
      }
    }
  }

  QString ImageCache::cacheKey(const QUrl& url) {
    QUrl clean = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveFragment);
    QUrlQuery query(clean);
    query.removeAllQueryItems(QStringLiteral("ApiKey"));
    query.removeAllQueryItems(QStringLiteral("api_key"));
    clean.setQuery(query);
    return clean.toString(QUrl::FullyEncoded);
  }

  QString ImageCache::memoryKey(const QString& key, QSize size) {
    return QStringLiteral("%1x%2 %3").arg(size.width()).arg(size.height()).arg(key);
  }

  QImage ImageCache::cached(const QUrl& url, QSize size) const {
    const QImage* image = m_memory.object(memoryKey(cacheKey(url), size));
    return image ? *image : QImage();
  }

  ImageCache::Ticket ImageCache::request(const QUrl& url, QSize size, QObject* context, Done done) {
    const Ticket ticket = ++m_nextTicket;
    if (!url.isValid() || url.isEmpty()) {
      done({});
      return ticket;
    }
    const QString key = cacheKey(url);
    if (const QImage* image = m_memory.object(memoryKey(key, size))) {
      done(*image);
      return ticket;
    }
    const QByteArray onDisk = readDisk(key);
    if (!onDisk.isEmpty()) {
      QImage image = decode(onDisk, size);
      if (!image.isNull()) {
        m_memory.insert(memoryKey(key, size), new QImage(image), costOf(image));
        done(image);
        return ticket;
      }
    }

    Waiter waiter{ticket, size, context, context != nullptr, std::move(done)};
    m_tickets.insert(ticket, key);
    auto it = m_fetches.find(key);
    if (it != m_fetches.end()) {
      it->waiters.append(std::move(waiter));
      // Asked for again: move to the front of the queue.
      if (m_queue.removeOne(key)) m_queue.append(key);
      return ticket;
    }
    Fetch fetch;
    fetch.url = url;
    fetch.waiters.append(std::move(waiter));
    m_fetches.insert(key, std::move(fetch));
    m_queue.append(key);
    pump();
    return ticket;
  }

  void ImageCache::cancel(Ticket ticket) {
    const QString key = m_tickets.take(ticket);
    if (key.isEmpty()) return;
    auto it = m_fetches.find(key);
    if (it == m_fetches.end()) return;
    it->waiters.removeIf([ticket](const Waiter& w) { return w.ticket == ticket; });
    if (!it->waiters.isEmpty()) return;
    if (QNetworkReply* reply = it->reply) {
      reply->disconnect(this);
      reply->abort();
      reply->deleteLater();
      m_running.removeOne(key);
    }
    m_queue.removeOne(key);
    m_fetches.erase(it);
    pump();
  }

  void ImageCache::pump() {
    while (m_running.size() < m_limits.parallelDownloads && !m_queue.isEmpty())
      start(m_queue.takeLast());
  }

  void ImageCache::start(const QString& key) {
    auto it = m_fetches.find(key);
    if (it == m_fetches.end()) return;
    QNetworkRequest request(it->url);
    const QUrl server = m_client.serverUrl();
    if (it->url.scheme() == server.scheme() && it->url.host() == server.host() &&
        it->url.port() == server.port() && m_client.isSignedIn())
      request.setRawHeader("Authorization", m_client.authorizationHeader());
    request.setTransferTimeout(30'000);
    QNetworkReply* reply = m_network->get(request);
    it->reply = reply;
    m_running.append(key);
    connect(reply, &QNetworkReply::finished, this, [this, key, reply]() {
      reply->deleteLater();
      m_running.removeOne(key);
      const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
        const QByteArray data = reply->readAll();
        writeDisk(key, data);
        deliver(key, data);
      } else {
        fail(key);
      }
      pump();
    });
  }

  void ImageCache::deliver(const QString& key, const QByteArray& encoded) {
    Fetch fetch = m_fetches.take(key);
    QHash<QString, QImage> decoded;
    for (const Waiter& waiter : fetch.waiters) {
      m_tickets.remove(waiter.ticket);
      const QString memKey = memoryKey(key, waiter.size);
      if (!decoded.contains(memKey)) {
        QImage image = decode(encoded, waiter.size);
        if (!image.isNull()) m_memory.insert(memKey, new QImage(image), costOf(image));
        decoded.insert(memKey, image);
      }
      if (waiter.hasContext && waiter.context.isNull()) continue;
      waiter.done(decoded.value(memKey));
    }
  }

  void ImageCache::fail(const QString& key) {
    const Fetch fetch = m_fetches.take(key);
    for (const Waiter& waiter : fetch.waiters) {
      m_tickets.remove(waiter.ticket);
      if (waiter.hasContext && waiter.context.isNull()) continue;
      waiter.done({});
    }
  }

  QString ImageCache::diskPath(const QString& key) const {
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1);
    return m_disk.filePath(QString::fromLatin1(hash.toHex()));
  }

  QByteArray ImageCache::readDisk(const QString& key) {
    if (!m_hasDisk) return {};
    QFile file(diskPath(key));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QByteArray data = file.readAll();
    file.close();
    // Recently used files survive trimming.
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return data;
  }

  void ImageCache::writeDisk(const QString& key, const QByteArray& data) {
    if (!m_hasDisk || data.isEmpty() || data.size() > m_limits.diskBytes) return;
    const QString path = diskPath(key);
    const qint64 previous = QFileInfo(path).exists() ? QFileInfo(path).size() : 0;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
      return;
    m_diskBytes += data.size() - previous;
    if (m_diskBytes > m_limits.diskBytes) trimDisk();
  }

  void ImageCache::trimDisk() {
    if (m_diskBytes <= m_limits.diskBytes) return;
    // Oldest first, down to 90 % of the limit so trimming is not repeated
    // on every write.
    QFileInfoList files = m_disk.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);
    const qint64 target = m_limits.diskBytes * 9 / 10;
    for (const QFileInfo& file : files) {
      if (m_diskBytes <= target) break;
      if (QFile::remove(file.filePath())) m_diskBytes -= file.size();
    }
  }

} // namespace jfd::library
