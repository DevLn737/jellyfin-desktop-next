#include <jfd/offline/OfflineCatalog.h>

namespace jfd::offline {

  QString OfflineCatalog::key(const QString& serverId, const QString& userId,
                              const QString& itemId) {
    return serverId + QLatin1Char('/') + userId + QLatin1Char('/') + itemId;
  }

  qsizetype OfflineCatalog::load() {
    m_items.clear();
    const QList<ItemManifest> manifests = m_store.scan();
    m_items.reserve(manifests.size());
    for (const ItemManifest& m : manifests)
      m_items.insert(key(m.serverId, m.userId, m.itemId), m);
    return m_items.size();
  }

  std::optional<ItemManifest> OfflineCatalog::find(const QString& serverId, const QString& userId,
                                                   const QString& itemId) const {
    const auto it = m_items.constFind(key(serverId, userId, itemId));
    if (it == m_items.constEnd()) return std::nullopt;
    return *it;
  }

  QList<ItemManifest> OfflineCatalog::items(const QString& serverId, const QString& userId) const {
    QList<ItemManifest> out;
    for (const ItemManifest& m : m_items)
      if (m.serverId == serverId && m.userId == userId) out.append(m);
    return out;
  }

  void OfflineCatalog::update(const ItemManifest& m) {
    m_items.insert(key(m.serverId, m.userId, m.itemId), m);
  }

  void OfflineCatalog::remove(const QString& serverId, const QString& userId,
                              const QString& itemId) {
    m_items.remove(key(serverId, userId, itemId));
  }

  std::int64_t OfflineCatalog::estimatedBytes() const {
    std::int64_t total = 0;
    for (const ItemManifest& m : m_items)
      total += m.expectedSize;
    return total;
  }

} // namespace jfd::offline
