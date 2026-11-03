#pragma once

#include <jfd/music/MusicQueue.h>

#include <QAbstractListModel>

namespace jfd::music {

  // The queue as a QML list: the Queue panel and the Now Playing page.
  //
  // Every edit of the queue goes through the model, which tells the views
  // before it changes anything: adding, removing and moving tracks are row
  // insertions, removals and moves, so a list keeps its place and its
  // delegates. Only edits that change the whole order (a new queue, shuffle
  // on or off, a restored queue) reset the model. With shuffle on, adding
  // and moving still touch only the rows they name: the queue mixes only
  // when shuffle is turned on or a new queue starts.
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

    // The queue shown and edited; it must outlive the model.
    void setQueue(MusicQueue* queue);

    // MusicQueue's edits, with the matching model signals.
    void replace(QList<Track> tracks, int start);
    void setShuffle(bool on);
    void append(QList<Track> tracks);
    void playNext(QList<Track> tracks);
    MusicQueue::Removed remove(int index);
    void move(int from, int to);
    void clear();
    // A queue restored from a saved state.
    void assign(MusicQueue queue);

    // Only the current track moved.
    void currentChanged(int before, int after);

  private:
    MusicQueue* m_queue = nullptr;
  };

} // namespace jfd::music
