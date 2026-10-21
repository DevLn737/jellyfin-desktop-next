#include "jfd/ui/LibraryImageProvider.h"

#include <jfd/library/ImageCache.h>

#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickTextureFactory>
#include <QQuickWindow>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace jfd::ui {

  namespace {

    // An image on screen, counted against the budget for as long as Qt
    // Quick keeps it.
    class CountedTextureFactory final : public QQuickTextureFactory {
    public:
      CountedTextureFactory(QImage image, TextureBudget& budget)
          : m_image(std::move(image)), m_budget(budget), m_bytes(m_image.sizeInBytes()) {
        m_budget.add(m_bytes);
      }
      ~CountedTextureFactory() override { m_budget.remove(m_bytes); }

      QSGTexture* createTexture(QQuickWindow* window) const override {
        return window->createTextureFromImage(m_image);
      }
      QSize textureSize() const override { return m_image.size(); }
      int textureByteCount() const override { return static_cast<int>(m_bytes); }
      QImage image() const override { return m_image; }

    private:
      QImage m_image;
      TextureBudget& m_budget;
      qint64 m_bytes;
    };

    class Response;

    // Shared between the response (on Qt Quick's image loader thread) and
    // the cache callback (on the cache's thread). The response is deleted by
    // Qt Quick only after it has emitted finished(), which happens once,
    // under the lock.
    struct Shared {
      QMutex mutex;
      Response* response = nullptr;
      bool done = false;
      library::ImageCache::Ticket ticket = 0;
    };

    class Response final : public QQuickImageResponse {
    public:
      Response(std::shared_ptr<Shared> shared, library::ImageCache& cache, TextureBudget& budget)
          : m_shared(std::move(shared)), m_cache(cache), m_budget(budget) {
        m_shared->response = this;
      }
      ~Response() override {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->response = nullptr;
      }

      QQuickTextureFactory* textureFactory() const override {
        return m_image.isNull() ? nullptr : new CountedTextureFactory(m_image, m_budget);
      }
      QString errorString() const override { return m_error; }

      void cancel() override {
        {
          QMutexLocker lock(&m_shared->mutex);
          if (m_shared->done) return;
          m_shared->done = true;
          m_error = QStringLiteral("Cancelled");
          Q_EMIT finished();
        }
        // The cache lives on another thread; a request it has not started
        // never reaches the network.
        QMetaObject::invokeMethod(&m_cache, [cache = &m_cache, shared = m_shared] {
          library::ImageCache::Ticket ticket = 0;
          {
            QMutexLocker lock(&shared->mutex);
            ticket = shared->ticket;
          }
          if (ticket != 0) cache->cancel(ticket);
        });
      }

      // With the lock held.
      void complete(const QImage& image) {
        m_image = image;
        if (image.isNull()) m_error = QStringLiteral("The image could not be loaded");
        Q_EMIT finished();
      }

    private:
      std::shared_ptr<Shared> m_shared;
      library::ImageCache& m_cache;
      TextureBudget& m_budget;
      QImage m_image;
      QString m_error;
    };

  } // namespace

  QSize TextureBudget::allowedSize(QSize requested) const {
    const qint64 now = used();
    if (now < m_limit) return requested;
    const int divisor = now < m_limit + m_limit / 2 ? 2 : 4;
    return QSize(std::max(1, requested.width() / divisor),
                 std::max(1, requested.height() / divisor));
  }

  LibraryImageProvider::LibraryImageProvider(library::ImageCache& cache, TextureBudget& budget,
                                             QSize maxSize)
      : m_cache(cache), m_budget(budget), m_maxSize(maxSize) {}

  QString LibraryImageProvider::sourceFor(const QUrl& url) {
    if (url.isEmpty()) return {};
    return QStringLiteral("image://%1/%2")
        .arg(QLatin1String(Id),
             QString::fromLatin1(QUrl::toPercentEncoding(url.toString(QUrl::FullyEncoded))));
  }

  QQuickImageResponse* LibraryImageProvider::requestImageResponse(const QString& id,
                                                                  const QSize& requestedSize) {
    auto shared = std::make_shared<Shared>();
    auto* response = new Response(shared, m_cache, m_budget);

    const QUrl url(QUrl::fromPercentEncoding(id.toLatin1()), QUrl::StrictMode);
    // A missing dimension (sourceSize with only a width) is bounded by the
    // largest size instead.
    QSize size(requestedSize.width() > 0 ? std::min(requestedSize.width(), m_maxSize.width())
                                         : m_maxSize.width(),
               requestedSize.height() > 0 ? std::min(requestedSize.height(), m_maxSize.height())
                                          : m_maxSize.height());
    size = m_budget.allowedSize(size);

    if (!url.isValid() || url.isRelative()) {
      QMutexLocker lock(&shared->mutex);
      shared->done = true;
      response->complete({});
      return response;
    }

    QMetaObject::invokeMethod(&m_cache, [cache = &m_cache, shared, url, size] {
      {
        QMutexLocker lock(&shared->mutex);
        if (shared->done) return;
      }
      const auto ticket = cache->request(url, size, cache, [shared](const QImage& image) {
        QMutexLocker lock(&shared->mutex);
        if (shared->done || shared->response == nullptr) return;
        shared->done = true;
        shared->response->complete(image);
      });
      QMutexLocker lock(&shared->mutex);
      shared->ticket = ticket;
    });
    return response;
  }

} // namespace jfd::ui
