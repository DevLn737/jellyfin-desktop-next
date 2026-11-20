#include <jfd/offline/OfflineCatalog.h>

#include <QSet>

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

  qsizetype OfflineCatalog::migrateLegacy() {
    QSet<QString> taken;
    QList<ItemManifest> legacy;
    for (const ItemManifest& m : std::as_const(m_items)) {
      if (m.format == 1) {
        legacy.append(m);
        continue;
      }
      taken.insert(m.mediaFile.toLower());
    }
    qsizetype moved = 0;
    for (const ItemManifest& m : std::as_const(legacy))
      if (const auto now = m_store.migrate(m, taken)) {
        update(*now);
        ++moved;
      }
    return moved;
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
