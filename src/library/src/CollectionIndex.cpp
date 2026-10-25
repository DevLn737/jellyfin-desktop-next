#include <jfd/library/CollectionIndex.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/LibraryUpdates.h>

#include <utility>

namespace jfd::library {

  namespace {
    // Enough for any real server; collections past it are not looked at.
    constexpr int MaxCollections = 2000;
  } // namespace

  CollectionIndex::CollectionIndex(api::JellyfinClient& client, QObject* parent)
      : QObject(parent), m_client(client) {}

  void CollectionIndex::collectionsOf(const QString& itemId, QObject* context, Answer done) {
    if (m_ready) {
      done(lookup(itemId));
      return;
    }
    m_waiting.append({itemId, context, std::move(done)});
    if (!m_building) build();
  }

  void CollectionIndex::invalidate() {
    ++m_generation;
    m_ready = false;
    m_building = false;
    m_pending = 0;
    m_collections.clear();
    m_members.clear();
    // Questions asked meanwhile get answers from the new index.
    if (!m_waiting.isEmpty()) build();
  }

  void CollectionIndex::follow(LibraryUpdates& updates) {
    connect(
        &updates, &LibraryUpdates::libraryChanged, this,
        [this](const QStringList& added, const QStringList& updated, const QStringList& removed) {
          // A collection that gains or loses an item is "updated".
          if (!added.isEmpty() || !updated.isEmpty() || !removed.isEmpty()) invalidate();
        });
    connect(&updates, &LibraryUpdates::resynchronize, this, &CollectionIndex::invalidate);
  }

  void CollectionIndex::build() {
    m_building = true;
    const quint64 generation = m_generation;
    api::ItemQuery query;
    query.includeItemTypes = {QStringLiteral("BoxSet")};
    query.recursive = true;
    query.sortBy = QStringLiteral("SortName");
    query.limit = MaxCollections;
    m_client.items(query, this, [this, generation](api::Result<api::ItemPage> result) {
      if (generation != m_generation) return;
      if (!result) {
        // Answer with nothing now and try again on the next question.
        finish(generation, false);
        return;
      }
      m_collections = result->items;
      m_pending = static_cast<int>(m_collections.size());
      if (m_pending == 0) {
        finish(generation, true);
        return;
      }
      for (int row = 0; row < m_collections.size(); ++row) {
        m_client.collectionItemIds(m_collections[row].id, this,
                                   [this, generation, row](api::Result<QStringList> ids) {
                                     if (generation != m_generation) return;
                                     if (ids) {
                                       for (const QString& id : ids.value())
                                         m_members[id].append(row);
                                     }
                                     if (--m_pending == 0) finish(generation, true);
                                   });
      }
    });
  }

  void CollectionIndex::finish(quint64 generation, bool complete) {
    if (generation != m_generation) return;
    m_building = false;
    m_ready = complete;
    const QList<Waiter> waiting = std::exchange(m_waiting, {});
    for (const Waiter& waiter : waiting) {
      if (waiter.context) waiter.done(lookup(waiter.itemId));
    }
    Q_EMIT built();
  }

  QList<api::Item> CollectionIndex::lookup(const QString& itemId) const {
    QList<api::Item> out;
    for (int row : m_members.value(itemId))
      out.append(m_collections[row]);
    return out;
  }

} // namespace jfd::library
