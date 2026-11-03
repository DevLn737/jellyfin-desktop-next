#include <jfd/music/MusicQueueModel.h>

namespace jfd::music {

  int MusicQueueModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !m_queue ? 0 : m_queue->size();
  }

  QVariant MusicQueueModel::data(const QModelIndex& index, int role) const {
    if (!m_queue || !index.isValid() || index.row() < 0 || index.row() >= m_queue->size()) return {};
    const Track& track = m_queue->tracks()[index.row()];
    switch (role) {
    case TrackId: return track.id;
    case EntryId: return static_cast<qlonglong>(track.entryId);
    case Title: return track.name;
    case Artist: return track.artist;
    case Album: return track.album;
    case Duration: return static_cast<double>(track.runTimeTicks) / 1e7;
    case ImageItemId: return track.imageItemId;
    case ImageTag: return track.imageTag;
    case IsCurrent: return index.row() == m_queue->current();
    }
    return {};
  }

  QHash<int, QByteArray> MusicQueueModel::roleNames() const {
    return {{TrackId, "trackId"},       {EntryId, "entryId"},         {Title, "title"},
            {Artist, "artist"},         {Album, "album"},             {Duration, "duration"},
            {ImageItemId, "imageItemId"}, {ImageTag, "imageTag"},     {IsCurrent, "isCurrent"}};
  }

  void MusicQueueModel::setQueue(MusicQueue* queue) {
    beginResetModel();
    m_queue = queue;
    endResetModel();
  }

  void MusicQueueModel::replace(QList<Track> tracks, int start) {
    beginResetModel();
    m_queue->replace(std::move(tracks), start);
    endResetModel();
  }

  void MusicQueueModel::setShuffle(bool on) {
    if (on == m_queue->shuffle()) return;
    // Turning shuffle on mixes what follows the current track; turning it
    // off puts every track back: the whole order changes.
    beginResetModel();
    m_queue->setShuffle(on);
    endResetModel();
  }

  void MusicQueueModel::append(QList<Track> tracks) {
    if (tracks.isEmpty()) return;
    const int first = m_queue->size();
    beginInsertRows({}, first, first + static_cast<int>(tracks.size()) - 1);
    m_queue->append(std::move(tracks));
    endInsertRows();
  }

  void MusicQueueModel::playNext(QList<Track> tracks) {
    if (tracks.isEmpty()) return;
    // MusicQueue puts them right after the current track, shuffled or not.
    const int first = m_queue->current() + 1;
    beginInsertRows({}, first, first + static_cast<int>(tracks.size()) - 1);
    m_queue->playNext(std::move(tracks));
    endInsertRows();
  }

  MusicQueue::Removed MusicQueueModel::remove(int index) {
    if (index < 0 || index >= m_queue->size()) return MusicQueue::Removed::Other;
    beginRemoveRows({}, index, index);
    const MusicQueue::Removed removed = m_queue->remove(index);
    endRemoveRows();
    // The track that followed (or the last one) is the current one now.
    if (removed == MusicQueue::Removed::Current) currentChanged(-1, m_queue->current());
    return removed;
  }

  void MusicQueueModel::move(int from, int to) {
    const int size = m_queue->size();
    if (from < 0 || from >= size || to < 0 || to >= size || from == to) return;
    // Qt names the row the moved one goes before: one further when moving down.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) return;
    m_queue->move(from, to);
    endMoveRows();
  }

  void MusicQueueModel::clear() {
    if (m_queue->isEmpty()) {
      m_queue->clear();
      return;
    }
    beginRemoveRows({}, 0, m_queue->size() - 1);
    m_queue->clear();
    endRemoveRows();
  }

  void MusicQueueModel::assign(MusicQueue queue) {
    beginResetModel();
    *m_queue = std::move(queue);
    endResetModel();
  }

  void MusicQueueModel::currentChanged(int before, int after) {
    for (const int row : {before, after})
      if (row >= 0 && m_queue && row < m_queue->size())
        Q_EMIT dataChanged(index(row), index(row), {IsCurrent});
  }

} // namespace jfd::music
