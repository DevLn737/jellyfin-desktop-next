#pragma once

#include <jfd/api/Models.h>

#include <QAbstractListModel>
#include <QList>
#include <QUrl>

namespace jfd::library {

  // Watched part of a partly watched item, 0..1; 0 when unstarted or played.
  double progressOf(const api::Item& item);
  // User data from an update (a favorite answer, UserDataChanged) applied
  // over what a listing said.
  api::UserItemData mergeUserData(const api::UserItemData& old, const api::UserItemData& update);

  // A list of items for QML: a row of cards, the seasons or episodes of a
  // series, search results.
  class ItemListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      NameRole,
      TypeRole,
      // "S1:E2", the production year, or empty.
      SubtitleRole,
      ImageUrlRole,
      RunTimeTicksRole,
      PlayedRole,
      PlaybackPositionTicksRole,
      IsFolderRole,
      FavoriteRole,
      // Episodes not yet watched in a series or season; 0 for other items.
      UnplayedCountRole,
      // Watched part of a partly watched item, 0..1; 0 when not started.
      ProgressRole,
      SeriesNameRole,
      OverviewRole,
      YearRole,
      CommunityRatingRole,
    };
    Q_ENUM(Role)

    struct Entry {
      api::Item item;
      QUrl imageUrl;
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    static QVariant entryData(const Entry& entry, int role);
    static QHash<int, QByteArray> itemRoleNames();

    int count() const { return static_cast<int>(m_entries.size()); }
    const Entry* entry(int row) const;
    void setEntries(QList<Entry> entries);
    // Updates every row of the item; true when one changed.
    bool applyUserData(const api::UserItemData& data);
    int rowOf(const QString& itemId) const;

  Q_SIGNALS:
    void countChanged();

  private:
    QList<Entry> m_entries;
  };

} // namespace jfd::library
