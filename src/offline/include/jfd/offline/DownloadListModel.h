#pragma once

#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/Manifest.h>

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>

#include <cstdint>

// A list of downloads for the screens (the "Downloads" page): the queue, or
// what is on disk. DownloadsController fills it; rows keep their identity
// across updates (moves, inserts and removals, never a reset), so the
// keyboard focus stays on the row it was on while the queue changes.
namespace jfd::offline {

  class DownloadListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Rows whose name or subtitle contain it (case-insensitive); empty: all.
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

  public:
    enum Role {
      ItemIdRole = Qt::UserRole + 1,
      NameRole,
      // "Series · S1:E2", "Artist · Album", the year.
      SubtitleRole,
      // The server's type: "Movie", "Episode", "Audio", ...
      TypeRole,
      // "queued", "paused", "failed", "complete".
      StateRole,
      // "idle", "transferring", "retrying".
      ActivityRole,
      // 0..1; 0 while the size is unknown.
      ProgressRole,
      BytesOnDiskRole,
      TotalBytesRole,
      // What the row says under its name, translated.
      StatusTextRole,
      // A local image of the item (file URL) or empty.
      ImageUrlRole,
      PlayedRole,
      PlaybackPositionTicksRole,
      RunTimeTicksRole,
      // The series or album the item belongs to, and its name.
      GroupIdRole,
      GroupNameRole,
      AlbumIdRole,
      RemovedOnServerRole,
      ChangedOnServerRole,
    };
    Q_ENUM(Role)

    struct Row {
      QString itemId;
      QString name;
      QString subtitle;
      QString type;
      ItemState state = ItemState::Queued;
      Activity activity = Activity::Idle;
      std::int64_t bytesOnDisk = 0;
      std::int64_t total = 0;
      QString statusText;
      QUrl imageUrl;
      bool played = false;
      std::int64_t positionTicks = 0;
      std::int64_t runTimeTicks = 0;
      QString groupId;
      QString groupName;
      QString albumId;
      bool removedOnServer = false;
      bool changedOnServer = false;

      bool operator==(const Row&) const = default;
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rows.size()); }
    QString filterText() const { return m_filter; }
    void setFilterText(const QString& text);

    // All rows, in order; the filter decides which are shown. Rows already
    // shown are updated in place or moved.
    void setRows(QList<Row> rows);
    // One row's transfer, without touching the others.
    void updateProgress(const QString& itemId, std::int64_t bytesOnDisk, std::int64_t total,
                        const QString& statusText);

    const Row* row(int index) const;
    int rowOf(const QString& itemId) const;
    Q_INVOKABLE QString itemIdAt(int index) const;

  Q_SIGNALS:
    void countChanged();
    void filterTextChanged();

  private:
    bool accepts(const Row& row) const;
    void apply();

    QList<Row> m_all;
    QList<Row> m_rows;
    QString m_filter;
  };

  QString itemStateKey(ItemState state);
  QString activityKey(Activity activity);

} // namespace jfd::offline
