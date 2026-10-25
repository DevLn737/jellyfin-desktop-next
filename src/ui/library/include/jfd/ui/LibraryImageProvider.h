#pragma once

#include <jfd/library/ImageCache.h>

#include <QPointer>
#include <QQuickAsyncImageProvider>
#include <QSize>

#include <atomic>
#include <memory>

namespace jfd::ui {

  // What the images on screen hold in video memory. Every image the
  // provider hands to Qt Quick is counted from the moment it is delivered
  // until Qt Quick lets go of it (its texture factory is destroyed when no
  // Image shows it and the pixmap cache drops it).
  //
  // Over the limit, new images are delivered at half their size, then a
  // quarter, so a long scroll on a laptop with shared video memory (Intel
  // Iris Xe) degrades to softer cards instead of running out of memory.
  // Images already on screen stay as they are.
  class TextureBudget {
  public:
    explicit TextureBudget(qint64 limitBytes) : m_limit(limitBytes) {}

    qint64 limit() const { return m_limit; }
    qint64 used() const { return m_used.load(std::memory_order_relaxed); }
    void add(qint64 bytes) { m_used.fetch_add(bytes, std::memory_order_relaxed); }
    void remove(qint64 bytes) { m_used.fetch_sub(bytes, std::memory_order_relaxed); }

    // The size to decode an image at, given the size its Image asks for.
    QSize allowedSize(QSize requested) const;

  private:
    const qint64 m_limit;
    std::atomic<qint64> m_used{0};
  };

  // image://jfd/<percent-encoded server URL>: card, backdrop and logo images
  // through the library's ImageCache (memory, disk, then the server),
  // decoded at the Image's sourceSize and counted against the texture
  // budget. Any size beyond `maxSize` is clamped to it.
  //
  // The engine owns the provider and Qt Quick the responses and textures,
  // and they can outlive the cache and the budget (the application shuts
  // the library down before its window; a texture is dropped when its Image
  // is). So the provider holds the cache weakly, answering nothing once it
  // is gone, and shares the budget.
  class LibraryImageProvider final : public QQuickAsyncImageProvider {
  public:
    static constexpr const char* Id = "jfd";
    // The default texture budget: room for a few screens of cards and a
    // backdrop at 4K.
    static constexpr qint64 DefaultTextureBytes = 192LL * 1024 * 1024;

    LibraryImageProvider(library::ImageCache& cache, std::shared_ptr<TextureBudget> budget,
                         QSize maxSize = QSize(3840, 2160));

    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

    // "image://jfd/..." for a server image URL; empty for an empty URL.
    static QString sourceFor(const QUrl& url);

  private:
    QPointer<library::ImageCache> m_cache;
    std::shared_ptr<TextureBudget> m_budget;
    QSize m_maxSize;
  };

} // namespace jfd::ui
