#pragma once

#include <QCache>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  // Images for cards, pages and the trickplay seek preview.
  //
  // Three levels: decoded images in memory under a byte limit (128 MiB by
  // default; on Intel Iris Xe video memory is system memory, so this also
  // bounds what the GPU side can be asked to upload), the encoded files on
  // disk in the profile under their own limit, and the server. Images are
  // decoded at the size the card asks for, never at the original size.
  //
  // Requests are queued newest first with a few running at once, so while
  // the user scrolls the cards now on screen load before the ones scrolled
  // past; a cancelled request that has not started never touches the
  // network, and one that has is aborted once nobody waits for it.
  //
  // The session token goes only to the client's own server (trickplay tiles
  // need it, images do not); any other URL is fetched without it.
  class ImageCache : public QObject {
    Q_OBJECT

  public:
    struct Limits {
      qint64 memoryBytes = 128LL * 1024 * 1024;
      qint64 diskBytes = 512LL * 1024 * 1024;
      int parallelDownloads = 6;
    };

    using Ticket = quint64;
    using Done = std::function<void(const QImage&)>;

    // `diskDirectory` empty: no disk cache.
    ImageCache(api::JellyfinClient& client, const QString& diskDirectory, Limits limits,
               QObject* parent = nullptr);
    ImageCache(api::JellyfinClient& client, const QString& diskDirectory, QObject* parent = nullptr)
        : ImageCache(client, diskDirectory, Limits{}, parent) {}
    ~ImageCache() override;

    // Delivers the image scaled to fit within `size` (an empty size keeps the
    // original size) to `done`, on this object's thread and only while
    // `context` lives. A null image means it could not be loaded. A cached
    // image is delivered before this returns.
    Ticket request(const QUrl& url, QSize size, QObject* context, Done done);
    void cancel(Ticket ticket);

    // The decoded image if it is in memory.
    QImage cached(const QUrl& url, QSize size) const;

    qint64 memoryBytes() const { return m_memory.totalCost(); }
    qint64 memoryLimit() const { return m_limits.memoryBytes; }
    qint64 diskBytes() const { return m_diskBytes; }
    int pendingDownloads() const { return static_cast<int>(m_queue.size()); }
    int runningDownloads() const { return static_cast<int>(m_running.size()); }
    void clearMemory() { m_memory.clear(); }

    // The key an URL is stored under: the URL without credentials.
    static QString cacheKey(const QUrl& url);

  private:
    struct Waiter {
      Ticket ticket = 0;
      QSize size;
      QPointer<QObject> context;
      bool hasContext = false;
      Done done;
    };
    struct Fetch {
      QUrl url;
      QList<Waiter> waiters;
      QNetworkReply* reply = nullptr;
    };

    static QString memoryKey(const QString& key, QSize size);
    void deliver(const QString& key, const QByteArray& encoded);
    void fail(const QString& key);
    void pump();
    void start(const QString& key);
    QString diskPath(const QString& key) const;
    QByteArray readDisk(const QString& key);
    void writeDisk(const QString& key, const QByteArray& data);
    void trimDisk();

    api::JellyfinClient& m_client;
    QNetworkAccessManager* m_network = nullptr;
    Limits m_limits;
    QDir m_disk;
    bool m_hasDisk = false;
    qint64 m_diskBytes = 0;
    QCache<QString, QImage> m_memory;
    QHash<QString, Fetch> m_fetches;
    // Keys waiting for a download slot, newest last.
    QList<QString> m_queue;
    QList<QString> m_running;
    QHash<Ticket, QString> m_tickets;
    Ticket m_nextTicket = 0;
  };

} // namespace jfd::library
