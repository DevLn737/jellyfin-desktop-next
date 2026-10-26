#include <jfd/ui/MusicItem.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/CardImages.h>

namespace jfd::ui {

  MusicItem::MusicItem(api::JellyfinClient& client, int imageWidth, qreal devicePixelRatio, QObject* parent)
      : QObject(parent), m_client(client), m_imageWidth(imageWidth), m_devicePixelRatio(devicePixelRatio) {}

  void MusicItem::setItemId(const QString& id) {
    if (id == m_itemId) return;
    m_itemId = id;
    m_item.reset();
    m_imageUrl.clear();
    m_errorText.clear();
    Q_EMIT itemChanged();
    if (id.isEmpty()) return;
    const quint64 serial = ++m_serial;
    m_client.item(id, this, [this, serial](api::Result<api::Item> result) {
      if (serial != m_serial) return;
      if (!result) {
        m_errorText = result.error().message;
      } else {
        m_item = *result;
        m_imageUrl = library::cardImageUrl(
            m_client, *result, library::CardShape::Square,
            library::cardImageSize(library::CardShape::Square, m_imageWidth, m_devicePixelRatio));
      }
      Q_EMIT itemChanged();
    });
  }

  QString MusicItem::artistName() const {
    if (!m_item) return {};
    if (!m_item->albumArtist.isEmpty()) return m_item->albumArtist;
    return m_item->artists.isEmpty() ? QString() : m_item->artists.front();
  }

  QString MusicItem::artistId() const {
    if (!m_item) return {};
    if (!m_item->albumArtists.isEmpty()) return m_item->albumArtists.front().id;
    return m_item->artistItems.isEmpty() ? QString() : m_item->artistItems.front().id;
  }

  QVariant MusicItem::year() const {
    return m_item && m_item->productionYear ? QVariant(*m_item->productionYear) : QVariant();
  }

  int MusicItem::trackCount() const {
    if (!m_item) return 0;
    if (m_item->songCount) return *m_item->songCount;
    return m_item->childCount.value_or(0);
  }

  void MusicItem::toggleFavorite() {
    if (!m_item) return;
    const bool wanted = !m_item->userData.isFavorite;
    m_client.setFavorite(m_itemId, wanted, this, [this, wanted](api::Result<api::UserItemData> result) {
      if (!m_item) return;
      m_item->userData.isFavorite = result ? result->isFavorite : wanted;
      Q_EMIT itemChanged();
    });
  }

} // namespace jfd::ui
