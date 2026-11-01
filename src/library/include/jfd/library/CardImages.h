#pragma once

#include <jfd/api/JellyfinClient.h>

#include <QSize>
#include <QUrl>

// Which server image a card shows. Posters are portrait (2:3), thumbs are
// landscape (16:9). Items without an image of their own borrow their
// series' or parent's, as Jellyfin Web does, so an episode in "Continue
// watching" still has a picture.
namespace jfd::library {

  enum class CardShape { Poster, Thumb, Square };

  // The logical widths to fetch card images for: a little above the largest
  // card the interface draws (Theme.posterWidth 190, thumbWidth 320 in a
  // spacious window), so a card is never stretched from a smaller picture.
  constexpr int PosterCardWidth = 220;
  constexpr int ThumbCardWidth = 360;

  // Card size in physical pixels for a card `width` logical pixels wide. The
  // image is fetched at no less than 1.5 times the logical size, even on a
  // screen with a ratio of 1, so scaling it down keeps the card sharp.
  QSize cardImageSize(CardShape shape, int width, qreal devicePixelRatio);

  // The best image for `item` on a card of `shape`; an empty URL when the
  // item and its parents have none.
  QUrl cardImageUrl(const api::JellyfinClient& client, const api::Item& item, CardShape shape,
                    QSize pixels);

  // A wide backdrop for a detail page; falls back to the series' backdrop.
  QUrl backdropUrl(const api::JellyfinClient& client, const api::Item& item, QSize pixels);
  QUrl logoUrl(const api::JellyfinClient& client, const api::Item& item, int maxWidth);

} // namespace jfd::library
