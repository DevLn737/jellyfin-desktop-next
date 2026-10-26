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

  void MusicQueueModel::reset(const MusicQueue* queue) {
    beginResetModel();
    m_queue = queue;
    endResetModel();
  }

  void MusicQueueModel::currentChanged(int before, int after) {
    for (const int row : {before, after})
      if (row >= 0 && m_queue && row < m_queue->size())
        Q_EMIT dataChanged(index(row), index(row), {IsCurrent});
  }

} // namespace jfd::music
