#include <jfd/library/CardImages.h>

#include <cmath>
#include <functional>
#include <initializer_list>

namespace jfd::library {

  namespace {
    QUrl image(const api::JellyfinClient& client, const QString& itemId, const QString& type,
               const QString& tag, QSize pixels, std::optional<int> index = std::nullopt) {
      if (itemId.isEmpty() || tag.isEmpty()) return {};
      api::ImageRequest request;
      request.itemId = itemId;
      request.type = type;
      request.tag = tag;
      request.index = index;
      request.fillWidth = pixels.width();
      request.fillHeight = pixels.height();
      return client.imageUrl(request);
    }

    const QString Primary = QStringLiteral("Primary");
    const QString Thumb = QStringLiteral("Thumb");
    const QString Backdrop = QStringLiteral("Backdrop");

    QUrl primary(const api::JellyfinClient& client, const api::Item& item, QSize pixels) {
      return image(client, item.id, Primary, item.primaryImageTag, pixels);
    }

    QUrl seriesPrimary(const api::JellyfinClient& client, const api::Item& item, QSize pixels) {
      return image(client, item.seriesId, Primary, item.seriesPrimaryImageTag, pixels);
    }

    QUrl ownBackdrop(const api::JellyfinClient& client, const api::Item& item, QSize pixels) {
      if (item.backdropImageTags.isEmpty()) return {};
      return image(client, item.id, Backdrop, item.backdropImageTags.front(), pixels, 0);
    }

    QUrl parentBackdrop(const api::JellyfinClient& client, const api::Item& item, QSize pixels) {
      if (item.parentBackdropImageTags.isEmpty()) return {};
      return image(client, item.parentBackdropItemId, Backdrop,
                   item.parentBackdropImageTags.front(), pixels, 0);
    }

    // The first choice that has an image.
    QUrl first(std::initializer_list<std::function<QUrl()>> choices) {
      for (const auto& choice : choices) {
        QUrl url = choice();
        if (!url.isEmpty()) return url;
      }
      return {};
    }
  } // namespace

  QSize cardImageSize(CardShape shape, int width, qreal devicePixelRatio) {
    const int w = static_cast<int>(std::lround(width * devicePixelRatio));
    switch (shape) {
    case CardShape::Poster:
      return {w, static_cast<int>(std::lround(w * 1.5))};
    case CardShape::Thumb:
      return {w, static_cast<int>(std::lround(w * 9.0 / 16.0))};
    case CardShape::Square:
      return {w, w};
    }
    return {w, w};
  }

  QUrl cardImageUrl(const api::JellyfinClient& client, const api::Item& item, CardShape shape,
                    QSize pixels) {
    auto own = [&] { return primary(client, item, pixels); };
    auto series = [&] { return seriesPrimary(client, item, pixels); };
    auto backdrop = [&] { return ownBackdrop(client, item, pixels); };
    auto parent = [&] { return parentBackdrop(client, item, pixels); };
    auto parentThumb = [&] {
      return image(client, item.parentThumbItemId, Thumb, item.parentThumbImageTag, pixels);
    };
    auto thumb = [&] { return image(client, item.id, Thumb, item.imageTags.value(Thumb), pixels); };
    const bool episode = item.type == QLatin1String("Episode");

    if (shape == CardShape::Thumb) {
      // An episode's own still first; a movie's or series' thumb or backdrop.
      if (episode) return first({own, parentThumb, parent});
      return first({thumb, backdrop, parent, own});
    }
    // An episode's poster is its series'.
    if (episode) return first({series, own});
    return first({own, series});
  }

  QUrl backdropUrl(const api::JellyfinClient& client, const api::Item& item, QSize pixels) {
    return first({[&] { return ownBackdrop(client, item, pixels); },
                  [&] { return parentBackdrop(client, item, pixels); }});
  }

  QUrl logoUrl(const api::JellyfinClient& client, const api::Item& item, int maxWidth) {
    const QString tag = item.imageTags.value(QStringLiteral("Logo"));
    if (tag.isEmpty()) return {};
    api::ImageRequest request;
    request.itemId = item.id;
    request.type = QStringLiteral("Logo");
    request.tag = tag;
    request.maxWidth = maxWidth;
    return client.imageUrl(request);
  }

} // namespace jfd::library
