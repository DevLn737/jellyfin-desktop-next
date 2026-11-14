#include <jfd/library/LiveTvOnNow.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ItemListModel.h>

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

namespace jfd::library {

  namespace {
    Q_LOGGING_CATEGORY(lcOnNow, "jfd.library.livetv")
  }

  LiveTvOnNow::LiveTvOnNow(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_api(client), m_options(std::move(options)) {
    m_tick.setInterval(m_options.tickMs);
    connect(&m_tick, &QTimer::timeout, this, &LiveTvOnNow::tick);
  }

  QDateTime LiveTvOnNow::now() const {
    return m_options.now ? m_options.now().toUTC() : QDateTime::currentDateTimeUtc();
  }

  int LiveTvOnNow::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  const LiveTvOnNow::Entry* LiveTvOnNow::entry(int row) const {
    if (row < 0 || row >= count()) return nullptr;
    return &m_entries[row];
  }

  QVariant LiveTvOnNow::data(const QModelIndex& index, int role) const {
    const Entry* e = entry(index.row());
    if (!e || index.parent().isValid()) return {};
    switch (role) {
    case Qt::DisplayRole:
    case ItemListModel::NameRole:
      return e->program.name;
    case ItemListModel::IdRole:
      return e->channel.id;
    case ItemListModel::TypeRole:
      return QStringLiteral("TvChannel");
    case ItemListModel::SubtitleRole:
      return e->channel.number.isEmpty() ? e->channel.name
                                         : e->channel.number + QStringLiteral(" · ") + e->channel.name;
    case ItemListModel::ImageUrlRole:
      return e->imageUrl;
    case ItemListModel::ProgressRole:
      return e->program.progressAt(now());
    case ItemListModel::OverviewRole:
      return e->program.overview;
    case ItemListModel::PlayedRole:
    case ItemListModel::IsFolderRole:
      return false;
    case ItemListModel::FavoriteRole:
      return e->channel.isFavorite;
    case ItemListModel::UnplayedCountRole:
      return 0;
    case ItemListModel::PlaybackPositionTicksRole:
      return QVariant::fromValue<qint64>(0);
    default:
      return {};
    }
  }

  QHash<int, QByteArray> LiveTvOnNow::roleNames() const { return ItemListModel::itemRoleNames(); }

  void LiveTvOnNow::reload() {
    const quint64 serial = ++m_serial;
    api::LiveTvChannelQuery query;
    query.limit = m_options.limit;
    query.addCurrentProgram = true;
    m_api.channels(query, this, [this, serial](api::Result<api::LiveTvChannelPage> page) {
      if (serial != m_serial) return;
      if (!page) {
        // The row keeps what it had; it is not worth an error on home.
        qCInfo(lcOnNow) << "on now:" << page.error().message;
        return;
      }
      const QDateTime at = now();
      const int pixels = static_cast<int>(
          std::lround(m_options.imageWidth * std::max<qreal>(1.0, m_options.devicePixelRatio)));
      QList<Entry> entries;
      for (const api::LiveTvChannel& c : page->channels) {
        if (!c.currentProgram || !c.currentProgram->isOnAt(at)) continue;
        Entry e;
        e.channel = c;
        e.program = *c.currentProgram;
        if (!c.primaryImageTag.isEmpty()) {
          api::ImageRequest image;
          image.itemId = c.id;
          image.tag = c.primaryImageTag;
          image.maxWidth = pixels;
          e.imageUrl = m_client.imageUrl(image);
        }
        entries.append(std::move(e));
      }
      setEntries(std::move(entries));
    });
  }

  void LiveTvOnNow::setEntries(QList<Entry> entries) {
    const bool countChanges = entries.size() != m_entries.size();
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    if (countChanges) Q_EMIT countChanged();
    if (m_entries.isEmpty()) m_tick.stop();
    else m_tick.start();
  }

  void LiveTvOnNow::tick() {
    if (m_entries.isEmpty()) return;
    const QDateTime at = now();
    const bool ended = std::any_of(m_entries.cbegin(), m_entries.cend(),
                                   [&](const Entry& e) { return !e.program.isOnAt(at); });
    if (ended) {
      reload();
      return;
    }
    Q_EMIT dataChanged(index(0), index(count() - 1), {ItemListModel::ProgressRole});
  }

  void LiveTvOnNow::clear() {
    ++m_serial;
    setEntries({});
  }

} // namespace jfd::library
