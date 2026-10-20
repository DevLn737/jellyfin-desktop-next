#include <jfd/library/ItemListModel.h>

#include <algorithm>

namespace jfd::library {

  namespace {
    QString subtitleFor(const api::Item& item) {
      if (item.type == QLatin1String("Episode") && item.indexNumber) {
        if (item.parentIndexNumber)
          return QStringLiteral("S%1:E%2").arg(*item.parentIndexNumber).arg(*item.indexNumber);
        return QStringLiteral("E%1").arg(*item.indexNumber);
      }
      if (item.productionYear) return QString::number(*item.productionYear);
      return {};
    }
  } // namespace

  double progressOf(const api::Item& item) {
    if (item.userData.played) return 0.0;
    if (item.userData.playedPercentage)
      return std::clamp(*item.userData.playedPercentage / 100.0, 0.0, 1.0);
    if (item.runTimeTicks && *item.runTimeTicks > 0 && item.userData.playbackPositionTicks > 0)
      return std::clamp(static_cast<double>(item.userData.playbackPositionTicks) /
                            static_cast<double>(*item.runTimeTicks),
                        0.0, 1.0);
    return 0.0;
  }

  api::UserItemData mergeUserData(const api::UserItemData& old, const api::UserItemData& update) {
    api::UserItemData out = update;
    // UserDataChanged carries no unplayed count for a single episode; keep
    // the folder's last one until the next listing.
    if (!out.unplayedItemCount) out.unplayedItemCount = old.unplayedItemCount;
    out.itemId.clear();
    return out;
  }

  int ItemListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant ItemListModel::data(const QModelIndex& index, int role) const {
    const Entry* e = entry(index.row());
    if (!e || index.parent().isValid()) return {};
    return entryData(*e, role);
  }

  QVariant ItemListModel::entryData(const Entry& entry, int role) {
    const Entry* e = &entry;
    const api::Item& item = e->item;
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
      return item.name;
    case IdRole:
      return item.id;
    case TypeRole:
      return item.type;
    case SubtitleRole:
      return subtitleFor(item);
    case ImageUrlRole:
      return e->imageUrl;
    case RunTimeTicksRole:
      return item.runTimeTicks ? QVariant::fromValue<qint64>(*item.runTimeTicks) : QVariant();
    case PlayedRole:
      return item.userData.played;
    case PlaybackPositionTicksRole:
      return QVariant::fromValue<qint64>(item.userData.playbackPositionTicks);
    case IsFolderRole:
      return item.isFolder;
    case FavoriteRole:
      return item.userData.isFavorite;
    case UnplayedCountRole:
      return item.userData.unplayedItemCount.value_or(0);
    case ProgressRole:
      return progressOf(item);
    case SeriesNameRole:
      return item.seriesName;
    case OverviewRole:
      return item.overview;
    case YearRole:
      return item.productionYear ? QVariant(*item.productionYear) : QVariant();
    case CommunityRatingRole:
      return item.communityRating ? QVariant(*item.communityRating) : QVariant();
    default:
      return {};
    }
  }

  QHash<int, QByteArray> ItemListModel::roleNames() const {
    return itemRoleNames();
  }

  QHash<int, QByteArray> ItemListModel::itemRoleNames() {
    return {{IdRole, "itemId"},
            {NameRole, "name"},
            {TypeRole, "type"},
            {SubtitleRole, "subtitle"},
            {ImageUrlRole, "imageUrl"},
            {RunTimeTicksRole, "runTimeTicks"},
            {PlayedRole, "played"},
            {PlaybackPositionTicksRole, "playbackPositionTicks"},
            {IsFolderRole, "isFolder"},
            {FavoriteRole, "favorite"},
            {UnplayedCountRole, "unplayedCount"},
            {ProgressRole, "progress"},
            {SeriesNameRole, "seriesName"},
            {OverviewRole, "overview"},
            {YearRole, "year"},
            {CommunityRatingRole, "communityRating"}};
  }

  const ItemListModel::Entry* ItemListModel::entry(int row) const {
    if (row < 0 || row >= count()) return nullptr;
    return &m_entries[row];
  }

  void ItemListModel::setEntries(QList<Entry> entries) {
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    Q_EMIT countChanged();
  }

  bool ItemListModel::applyUserData(const api::UserItemData& data) {
    bool changed = false;
    for (int row = 0; row < count(); ++row) {
      api::Item& item = m_entries[row].item;
      if (item.id != data.itemId) continue;
      item.userData = mergeUserData(item.userData, data);
      Q_EMIT dataChanged(index(row), index(row));
      changed = true;
    }
    return changed;
  }

  int ItemListModel::rowOf(const QString& itemId) const {
    for (int row = 0; row < count(); ++row)
      if (m_entries[row].item.id == itemId) return row;
    return -1;
  }

} // namespace jfd::library
