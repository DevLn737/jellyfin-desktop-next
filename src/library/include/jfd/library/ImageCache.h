#pragma once

#include <QCache>
#include <QDir>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QThreadPool>
#include <QUrl>

#include <functional>
#include <memory>

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
  // Files are read from disk and decoded on a small worker pool, never on
  // the cache's thread, so a fast scroll over cached posters does not cost
  // frames; only the bookkeeping runs on the cache's thread.
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

    // A way into the cache from other threads (Qt Quick's image loader).
    // post() hands `work` to the cache's thread only while the cache lives:
    // the destructor closes the gate under the same lock first, and Qt drops
    // the events still queued for a destroyed object. Checking a QPointer on
    // another thread and then posting to the raw pointer races with the
    // destructor; this does not.
    class Gate {
    public:
      // False when the cache is gone; `work` is not run then.
      bool post(std::function<void(ImageCache&)> work);

    private:
      friend class ImageCache;
      QMutex m_mutex;
      ImageCache* m_cache = nullptr;
    };

    // `diskDirectory` empty: no disk cache.
    ImageCache(api::JellyfinClient& client, const QString& diskDirectory, Limits limits,
               QObject* parent = nullptr);
    ImageCache(api::JellyfinClient& client, const QString& diskDirectory, QObject* parent = nullptr)
        : ImageCache(client, diskDirectory, Limits{}, parent) {}
    ~ImageCache() override;

    // Delivers the image scaled to fit within `size` (an empty size keeps the
    // original size) to `done`, on this object's thread and only while
    // `context` lives. A null image means it could not be loaded. An image
    // already decoded in memory is delivered before this returns; one on
    // disk a moment later, decoded off this thread.
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

    // Shared with the threads that post to the cache; see Gate.
    std::shared_ptr<Gate> gate() const { return m_gate; }

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
      // The background job (disk read and decode) this fetch waits for; 0
      // for none. An answer for another job is stale and dropped.
      quint64 job = 0;
      // The encoded image came from the disk cache: if it does not decode,
      // the server is asked instead.
      bool fromDisk = false;
    };
    struct Decoded {
      QSize size;
      QImage image;
    };

    static QString memoryKey(const QString& key, QSize size);
    // Reads `path` (when `encoded` is empty) and decodes at the sizes the
    // fetch's waiters ask for, on the worker pool; the answer comes back
    // through onDecoded.
    void decodeLater(const QString& key, Fetch& fetch, const QByteArray& encoded,
                     const QString& path);
    void onDecoded(const QString& key, quint64 job, const QByteArray& encoded,
                   const QList<Decoded>& images);
    void enqueue(const QString& key);
    void fail(const QString& key);
    void pump();
    void start(const QString& key);
    QString diskPath(const QString& key) const;
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
    quint64 m_nextJob = 0;
    std::shared_ptr<Gate> m_gate;
    // Declared last: destroyed (and waited for) first.
    QThreadPool m_pool;
  };

} // namespace jfd::library
