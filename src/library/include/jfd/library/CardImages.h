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

  // Card size in physical pixels for a card `width` logical pixels wide.
  QSize cardImageSize(CardShape shape, int width, qreal devicePixelRatio);

  // The best image for `item` on a card of `shape`; an empty URL when the
  // item and its parents have none.
  QUrl cardImageUrl(const api::JellyfinClient& client, const api::Item& item, CardShape shape,
                    QSize pixels);

  // A wide backdrop for a detail page; falls back to the series' backdrop.
  QUrl backdropUrl(const api::JellyfinClient& client, const api::Item& item, QSize pixels);
  QUrl logoUrl(const api::JellyfinClient& client, const api::Item& item, int maxWidth);

} // namespace jfd::library
