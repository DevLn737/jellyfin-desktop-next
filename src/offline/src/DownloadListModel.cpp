#include <jfd/offline/DownloadListModel.h>

#include <QSet>

namespace jfd::offline {

  QString itemStateKey(ItemState state) {
    switch (state) {
    case ItemState::Queued: return QStringLiteral("queued");
    case ItemState::Paused: return QStringLiteral("paused");
    case ItemState::Failed: return QStringLiteral("failed");
    case ItemState::Complete: return QStringLiteral("complete");
    }
    return {};
  }

  QString activityKey(Activity activity) {
    switch (activity) {
    case Activity::Idle: return QStringLiteral("idle");
    case Activity::Transferring: return QStringLiteral("transferring");
    case Activity::RetryWait: return QStringLiteral("retrying");
    }
    return {};
  }

  int DownloadListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant DownloadListModel::data(const QModelIndex& index, int role) const {
    const Row* r = row(index.row());
    if (r == nullptr || index.column() != 0) return {};
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return r->name;
    case ItemIdRole: return r->itemId;
    case SubtitleRole: return r->subtitle;
    case TypeRole: return r->type;
    case StateRole: return itemStateKey(r->state);
    case ActivityRole: return activityKey(r->activity);
    case ProgressRole:
      if (r->state == ItemState::Complete) return 1.0;
      return r->total > 0 ? std::min(1.0, double(r->bytesOnDisk) / double(r->total)) : 0.0;
    case BytesOnDiskRole: return qint64(r->bytesOnDisk);
    case TotalBytesRole: return qint64(r->total);
    case StatusTextRole: return r->statusText;
    case ImageUrlRole: return r->imageUrl;
    case PlayedRole: return r->played;
    case PlaybackPositionTicksRole: return qint64(r->positionTicks);
    case RunTimeTicksRole: return qint64(r->runTimeTicks);
    case GroupIdRole: return r->groupId;
    case GroupNameRole: return r->groupName;
    case AlbumIdRole: return r->albumId;
    case RemovedOnServerRole: return r->removedOnServer;
    case ChangedOnServerRole: return r->changedOnServer;
    default: return {};
    }
  }

  QHash<int, QByteArray> DownloadListModel::roleNames() const {
    return {
        {ItemIdRole, "itemId"},
        {NameRole, "name"},
        {SubtitleRole, "subtitle"},
        {TypeRole, "type"},
        {StateRole, "downloadState"},
        {ActivityRole, "activity"},
        {ProgressRole, "downloadProgress"},
        {BytesOnDiskRole, "bytesOnDisk"},
        {TotalBytesRole, "totalBytes"},
        {StatusTextRole, "statusText"},
        {ImageUrlRole, "imageUrl"},
        {PlayedRole, "played"},
        {PlaybackPositionTicksRole, "playbackPositionTicks"},
        {RunTimeTicksRole, "runTimeTicks"},
        {GroupIdRole, "groupId"},
        {GroupNameRole, "groupName"},
        {AlbumIdRole, "albumId"},
        {RemovedOnServerRole, "removedOnServer"},
        {ChangedOnServerRole, "changedOnServer"},
    };
  }

  const DownloadListModel::Row* DownloadListModel::row(int index) const {
    if (index < 0 || index >= m_rows.size()) return nullptr;
    return &m_rows[index];
  }

  int DownloadListModel::rowOf(const QString& itemId) const {
    for (int i = 0; i < m_rows.size(); ++i)
      if (m_rows[i].itemId == itemId) return i;
    return -1;
  }

  QString DownloadListModel::itemIdAt(int index) const {
    const Row* r = row(index);
    return r != nullptr ? r->itemId : QString();
  }

  void DownloadListModel::setFilterText(const QString& text) {
    if (text == m_filter) return;
    m_filter = text;
    Q_EMIT filterTextChanged();
    apply();
  }

  void DownloadListModel::setRows(QList<Row> rows) {
    m_all = std::move(rows);
    apply();
  }

  void DownloadListModel::updateProgress(const QString& itemId, std::int64_t bytesOnDisk,
                                         std::int64_t total, const QString& statusText) {
    for (Row& r : m_all) {
      if (r.itemId != itemId) continue;
      r.bytesOnDisk = bytesOnDisk;
      r.total = total;
      r.statusText = statusText;
    }
    const int i = rowOf(itemId);
    if (i < 0) return;
    Row& r = m_rows[i];
    r.bytesOnDisk = bytesOnDisk;
    r.total = total;
    r.statusText = statusText;
    const QModelIndex at = index(i);
    Q_EMIT dataChanged(at, at, {ProgressRole, BytesOnDiskRole, TotalBytesRole, StatusTextRole});
  }

  bool DownloadListModel::accepts(const Row& r) const {
    const QString filter = m_filter.trimmed();
    return filter.isEmpty() || r.name.contains(filter, Qt::CaseInsensitive) ||
           r.subtitle.contains(filter, Qt::CaseInsensitive) ||
           r.groupName.contains(filter, Qt::CaseInsensitive);
  }

  void DownloadListModel::apply() {
    QList<Row> wanted;
    for (const Row& r : std::as_const(m_all))
      if (accepts(r)) wanted.append(r);
    const int before = count();

    // 1. Rows that go.
    QSet<QString> keep;
    for (const Row& r : std::as_const(wanted)) keep.insert(r.itemId);
    for (int i = static_cast<int>(m_rows.size()) - 1; i >= 0; --i) {
      if (keep.contains(m_rows[i].itemId)) continue;
      beginRemoveRows({}, i, i);
      m_rows.removeAt(i);
      endRemoveRows();
    }
    // 2. Each place in order: the row already there, one moved up from
    // further down, or a new one.
    for (int i = 0; i < wanted.size(); ++i) {
      const Row& target = wanted[i];
      if (i < m_rows.size() && m_rows[i].itemId == target.itemId) {
        if (!(m_rows[i] == target)) {
          m_rows[i] = target;
          Q_EMIT dataChanged(index(i), index(i));
        }
        continue;
      }
      int from = -1;
      for (int j = i + 1; j < m_rows.size(); ++j)
        if (m_rows[j].itemId == target.itemId) {
          from = j;
          break;
        }
      if (from >= 0) {
        beginMoveRows({}, from, from, {}, i);
        m_rows.move(from, i);
        endMoveRows();
        if (!(m_rows[i] == target)) {
          m_rows[i] = target;
          Q_EMIT dataChanged(index(i), index(i));
        }
      } else {
        beginInsertRows({}, i, i);
        m_rows.insert(i, target);
        endInsertRows();
      }
    }
    if (count() != before) Q_EMIT countChanged();
  }

} // namespace jfd::offline
