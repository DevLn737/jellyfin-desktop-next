#pragma once

#include <jfd/api/Models.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  class LibraryUpdates;

  // Which collections (BoxSets) hold an item. The server lists a
  // collection's items but not an item's collections, so the index lists
  // every collection once, then the item ids of each, and answers from
  // that. It is built on the first question and built again on the first
  // question after the library changes.
  class CollectionIndex : public QObject {
    Q_OBJECT

  public:
    using Answer = std::function<void(const QList<api::Item>& collections)>;

    explicit CollectionIndex(api::JellyfinClient& client, QObject* parent = nullptr);

    // Calls `done` with the collections holding `itemId`, sorted by name: at
    // once when the index is built, otherwise when it is. Only while
    // `context` is alive. When the collections cannot be listed the answer
    // is empty and the next question tries again.
    void collectionsOf(const QString& itemId, QObject* context, Answer done);
    // Forgets the index (another server or user, or a library change).
    void invalidate();
    void follow(LibraryUpdates& updates);

    bool ready() const { return m_ready; }

  Q_SIGNALS:
    void built();

  private:
    struct Waiter {
      QString itemId;
      QPointer<QObject> context;
      Answer done;
    };

    void build();
    void finish(quint64 generation, bool complete);
    QList<api::Item> lookup(const QString& itemId) const;

    api::JellyfinClient& m_client;
    quint64 m_generation = 0;
    bool m_ready = false;
    bool m_building = false;
    int m_pending = 0;
    QList<api::Item> m_collections;
    // Item id -> rows of m_collections.
    QHash<QString, QList<int>> m_members;
    QList<Waiter> m_waiting;
  };

} // namespace jfd::library
