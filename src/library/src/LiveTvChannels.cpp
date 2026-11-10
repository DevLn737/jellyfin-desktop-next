#include <jfd/library/LiveTvChannels.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ErrorText.h>

#include <QHash>

#include <algorithm>
#include <cmath>

namespace jfd::library {

  namespace {
    // Load the guide again this long before the loaded window runs out.
    constexpr qint64 RefreshMarginSecs = 3600;
  } // namespace

  LiveTvChannels::LiveTvChannels(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_api(client), m_options(std::move(options)) {
    m_tick.setInterval(m_options.tickMs);
    connect(&m_tick, &QTimer::timeout, this, &LiveTvChannels::tick);
  }

  QDateTime LiveTvChannels::now() const {
    return m_options.now ? m_options.now().toUTC() : QDateTime::currentDateTimeUtc();
  }

  int LiveTvChannels::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  const LiveTvChannels::Row* LiveTvChannels::row(int index) const {
    if (index < 0 || index >= count()) return nullptr;
    return &m_rows[index];
  }

  std::optional<api::LiveTvProgram> LiveTvChannels::nowOn(int index, const QDateTime& at) const {
    const Row* r = row(index);
    if (!r) return std::nullopt;
    for (const api::LiveTvProgram& p : r->programs)
      if (p.isOnAt(at)) return p;
    // The channel list's own idea, while the guide window has nothing.
    if (r->channel.currentProgram && r->channel.currentProgram->isOnAt(at))
      return r->channel.currentProgram;
    return std::nullopt;
  }

  std::optional<api::LiveTvProgram> LiveTvChannels::nextOn(int index, const QDateTime& at) const {
    const Row* r = row(index);
    if (!r) return std::nullopt;
    const std::optional<api::LiveTvProgram> current = nowOn(index, at);
    // After what is on now, or the first one still to start.
    const QDateTime after = current ? current->end : at;
    for (const api::LiveTvProgram& p : r->programs)
      if (p.start >= after && (!current || p.id != current->id)) return p;
    return std::nullopt;
  }

  QVariant LiveTvChannels::data(const QModelIndex& index, int role) const {
    const Row* r = row(index.row());
    if (!r || index.parent().isValid()) return {};
    const api::LiveTvChannel& c = r->channel;
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
      return c.name;
    case IdRole:
      return c.id;
    case NumberRole:
      return c.number;
    case ImageUrlRole:
      return r->imageUrl;
    case FavoriteRole:
      return c.isFavorite;
    default:
      break;
    }
    const QDateTime at = now();
    switch (role) {
    case NowTitleRole: {
      const auto p = nowOn(index.row(), at);
      return p ? p->name : QString();
    }
    case NowStartRole: {
      const auto p = nowOn(index.row(), at);
      return p ? QVariant(p->start.toLocalTime()) : QVariant();
    }
    case NowEndRole: {
      const auto p = nowOn(index.row(), at);
      return p ? QVariant(p->end.toLocalTime()) : QVariant();
    }
    case NowProgressRole: {
      const auto p = nowOn(index.row(), at);
      return p ? p->progressAt(at) : 0.0;
    }
    case NextTitleRole: {
      const auto p = nextOn(index.row(), at);
      return p ? p->name : QString();
    }
    case NextStartRole: {
      const auto p = nextOn(index.row(), at);
      return p ? QVariant(p->start.toLocalTime()) : QVariant();
    }
    default:
      return {};
    }
  }

  QHash<int, QByteArray> LiveTvChannels::roleNames() const {
    return {{IdRole, "channelId"},       {NameRole, "name"},
            {NumberRole, "number"},      {ImageUrlRole, "imageUrl"},
            {FavoriteRole, "favorite"},  {NowTitleRole, "nowTitle"},
            {NowStartRole, "nowStart"},  {NowEndRole, "nowEnd"},
            {NowProgressRole, "nowProgress"}, {NextTitleRole, "nextTitle"},
            {NextStartRole, "nextStart"}};
  }

  void LiveTvChannels::checkAvailable() {
    const quint64 serial = ++m_infoSerial;
    m_api.info(this, [this, serial](api::Result<api::LiveTvInfo> result) {
      if (serial != m_infoSerial) return;
      // A server that cannot answer has no Live TV for us; a failure says
      // nothing about the guide, so only the entry goes.
      setAvailable(result && result->availableTo(m_client.userId()));
    });
  }

  void LiveTvChannels::reload() {
    const quint64 serial = ++m_serial;
    setLoading(true);
    setError({});
    loadChannels(serial, 0, {});
  }

  void LiveTvChannels::loadChannels(quint64 serial, int startIndex,
                                    QList<api::LiveTvChannel> channels) {
    api::LiveTvChannelQuery query;
    query.startIndex = startIndex;
    query.limit = m_options.channelPageSize;
    m_api.channels(query, this,
                   [this, serial, channels = std::move(channels)](
                       api::Result<api::LiveTvChannelPage> page) mutable {
                     if (serial != m_serial) return;
                     if (!page) return fail(page.error());
                     channels.append(page->channels);
                     const int next = page->startIndex + static_cast<int>(page->channels.size());
                     if (!page->channels.isEmpty() && next < page->totalRecordCount)
                       return loadChannels(serial, next, std::move(channels));
                     loadPrograms(serial, 0, std::move(channels), {});
                   });
  }

  void LiveTvChannels::loadPrograms(quint64 serial, int startIndex,
                                    QList<api::LiveTvChannel> channels,
                                    QList<api::LiveTvProgram> programs) {
    if (channels.isEmpty()) return finish({}, {});
    const QDateTime from = now();
    api::LiveTvProgramQuery query;
    query.from = from;
    query.to = from.addSecs(qint64(m_options.windowHours) * 3600);
    query.startIndex = startIndex;
    query.limit = m_options.programPageSize;
    m_windowEnd = query.to;
    m_api.programs(query, this,
                   [this, serial, channels = std::move(channels), programs = std::move(programs)](
                       api::Result<api::LiveTvProgramPage> page) mutable {
                     if (serial != m_serial) return;
                     if (!page) {
                       // The channels still play without a guide.
                       return finish(std::move(channels), std::move(programs));
                     }
                     const int got = static_cast<int>(page->programs.size());
                     programs.append(page->programs);
                     // Pages count every programme, dated or not.
                     const int next = page->startIndex + m_options.programPageSize;
                     if (got > 0 && next < page->totalRecordCount)
                       return loadPrograms(serial, next, std::move(channels), std::move(programs));
                     finish(std::move(channels), std::move(programs));
                   });
  }

  void LiveTvChannels::finish(QList<api::LiveTvChannel> channels,
                              QList<api::LiveTvProgram> programs) {
    QHash<QString, QList<api::LiveTvProgram>> byChannel;
    for (api::LiveTvProgram& p : programs)
      byChannel[p.channelId].append(std::move(p));
    QList<Row> rows;
    rows.reserve(channels.size());
    const int pixels =
        static_cast<int>(std::lround(m_options.imageWidth * std::max<qreal>(1.0, m_options.devicePixelRatio)));
    for (api::LiveTvChannel& c : channels) {
      Row r;
      r.programs = byChannel.take(c.id);
      std::sort(r.programs.begin(), r.programs.end(),
                [](const api::LiveTvProgram& a, const api::LiveTvProgram& b) { return a.start < b.start; });
      if (!c.primaryImageTag.isEmpty()) {
        api::ImageRequest image;
        image.itemId = c.id;
        image.tag = c.primaryImageTag;
        image.maxWidth = pixels;
        r.imageUrl = m_client.imageUrl(image);
      }
      r.channel = std::move(c);
      rows.append(std::move(r));
    }
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChanges) Q_EMIT countChanged();
    setLoading(false);
    if (!m_rows.isEmpty()) m_tick.start();
    else m_tick.stop();
  }

  void LiveTvChannels::tick() {
    if (m_rows.isEmpty()) return;
    if (!m_loading && m_windowEnd.isValid() && now().secsTo(m_windowEnd) < RefreshMarginSecs) {
      reload();
      return;
    }
    Q_EMIT dataChanged(index(0), index(count() - 1),
                       {NowTitleRole, NowStartRole, NowEndRole, NowProgressRole, NextTitleRole,
                        NextStartRole});
  }

  void LiveTvChannels::clear() {
    ++m_serial;
    ++m_infoSerial;
    m_tick.stop();
    m_windowEnd = {};
    const bool had = !m_rows.isEmpty();
    beginResetModel();
    m_rows.clear();
    endResetModel();
    if (had) Q_EMIT countChanged();
    setLoading(false);
    setError({});
    setAvailable(false);
  }

  void LiveTvChannels::fail(const api::ApiError& error) {
    setLoading(false);
    setError(library::errorText(error));
  }

  void LiveTvChannels::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void LiveTvChannels::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

  void LiveTvChannels::setAvailable(bool available) {
    if (m_available == available) return;
    m_available = available;
    Q_EMIT availableChanged();
  }

} // namespace jfd::library
