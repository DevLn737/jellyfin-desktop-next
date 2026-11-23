#pragma once

#include <jfd/api/Models.h>

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace jfd::library {

  // Watched part of a partly watched item, 0..1; 0 when unstarted or played.
  double progressOf(const api::Item& item);
  // User data from an update (a favorite answer, UserDataChanged) applied
  // over what a listing said.
  api::UserItemData mergeUserData(const api::UserItemData& old, const api::UserItemData& update);
  // An episode's short number in the interface language: "S1:E3", or "E3"
  // without a season; empty without an episode number.
  QString episodeNumber(std::optional<int> season, std::optional<int> episode);

  // A list of items for QML: a row of cards, the seasons or episodes of a
  // series, search results.
  class ItemListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // How many rows are played, kept up as user data changes (a season's
    // "3 of 600 watched" without walking its delegates).
    Q_PROPERTY(int playedCount READ playedCount NOTIFY playedCountChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      NameRole,
      TypeRole,
      // "S1:E2", a track's artists, the production year, or empty.
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
      // A track's album, so it can play in its album.
      AlbumIdRole,
      SeriesIdRole,
      GenresRole,
      OfficialRatingRole,
      // Set only on the lists that fetch them (the home page's featured
      // items); empty otherwise.
      BackdropUrlRole,
      LogoUrlRole,
      // A library's or folder's number of items, when the server gave it.
      ChildCountRole,
      // Why a home banner item is there: "resume", "nextUp" or "latest";
      // empty on other lists.
      FeaturedReasonRole,
      // An episode's number in its season; empty for other items.
      IndexNumberRole,
      // Models that add roles of their own start after this one.
      LastRole = IndexNumberRole,
    };
    Q_ENUM(Role)

    struct Entry {
      api::Item item;
      QUrl imageUrl;
      QUrl backdropUrl = {};
      QUrl logoUrl = {};
      QString featuredReason = {};
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
    Q_INVOKABLE int rowOf(const QString& itemId) const;
    int playedCount() const { return m_playedCount; }
    // The first row not played; 0 when every row is (or there are none).
    Q_INVOKABLE int firstUnplayedRow() const;

  Q_SIGNALS:
    void countChanged();
    void playedCountChanged();

  private:
    void countPlayed();

    QList<Entry> m_entries;
    int m_playedCount = 0;
  };

} // namespace jfd::library
