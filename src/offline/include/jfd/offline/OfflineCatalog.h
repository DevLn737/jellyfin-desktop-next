#pragma once

#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/Manifest.h>

#include <QHash>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

// The in-memory index of everything under the download folder, built from
// the manifests at startup (10,000 of them in under a second; see the
// catalog test). Downloads of every server and user are indexed; screens
// ask for the signed-in user's only, so another user's downloads stay
// hidden but kept (sign-out keeps them).
namespace jfd::offline {

  class OfflineCatalog {
  public:
    explicit OfflineCatalog(const DownloadStore& store) : m_store(store) {}

    // Rebuilds the index from disk; returns the number of items.
    qsizetype load();

    qsizetype size() const { return m_items.size(); }
    std::optional<ItemManifest> find(const QString& serverId, const QString& userId,
                                     const QString& itemId) const;
    QList<ItemManifest> items(const QString& serverId, const QString& userId) const;
    QList<ItemManifest> all() const { return m_items.values(); }

    // Keeps the index in step with a manifest just written or removed.
    void update(const ItemManifest& manifest);
    void remove(const QString& serverId, const QString& userId, const QString& itemId);

    // The sum of expected sizes of all items (complete or not): what the
    // space limit is checked against.
    std::int64_t estimatedBytes() const;

  private:
    static QString key(const QString& serverId, const QString& userId, const QString& itemId);

    const DownloadStore& m_store;
    QHash<QString, ItemManifest> m_items;
  };

} // namespace jfd::offline
