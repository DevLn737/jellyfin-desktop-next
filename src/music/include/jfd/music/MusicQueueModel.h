#pragma once

#include <jfd/music/MusicQueue.h>

#include <QAbstractListModel>

namespace jfd::music {

  // The queue as a QML list: the Queue panel and the Now Playing page.
  class MusicQueueModel : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
      TrackId = Qt::UserRole + 1,
      EntryId,
      Title,
      Artist,
      Album,
      Duration,
      ImageItemId,
      ImageTag,
      IsCurrent,
    };

    explicit MusicQueueModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The queue changed in structure.
    void reset(const MusicQueue* queue);
    // Only the current track moved.
    void currentChanged(int before, int after);

  private:
    const MusicQueue* m_queue = nullptr;
  };

} // namespace jfd::music
