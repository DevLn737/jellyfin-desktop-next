#include <jfd/library/LiveTvSchedule.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ErrorText.h>

#include <algorithm>
#include <cmath>

namespace jfd::library {

  LiveTvSchedule::LiveTvSchedule(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_api(client), m_options(std::move(options)) {
    m_tick.setInterval(m_options.tickMs);
    connect(&m_tick, &QTimer::timeout, this, &LiveTvSchedule::tick);
  }

  QDateTime LiveTvSchedule::now() const {
    return m_options.now ? m_options.now().toUTC() : QDateTime::currentDateTimeUtc();
  }

  int LiveTvSchedule::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant LiveTvSchedule::data(const QModelIndex& index, int role) const {
    if (index.parent().isValid() || index.row() < 0 || index.row() >= count()) return {};
    const api::LiveTvProgram& p = m_programs[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
      return p.name;
    case IdRole:
      return p.id;
    case EpisodeTitleRole:
      return p.episodeTitle;
    case OverviewRole:
      return p.overview;
    case StartRole:
      return p.start.toLocalTime();
    case EndRole:
      return p.end.toLocalTime();
    case OnNowRole:
      return p.isOnAt(now());
    case ProgressRole:
      return p.progressAt(now());
    case IsSeriesRole:
      return p.isSeries;
    case TimerIdRole:
      return p.timerId;
    case SeriesTimerIdRole:
      return p.seriesTimerId;
    default:
      return {};
    }
  }

  QHash<int, QByteArray> LiveTvSchedule::roleNames() const {
    return {{IdRole, "programId"},   {TitleRole, "title"},   {EpisodeTitleRole, "episodeTitle"},
            {OverviewRole, "overview"}, {StartRole, "start"}, {EndRole, "end"},
            {OnNowRole, "onNow"},     {ProgressRole, "progress"},
            {IsSeriesRole, "isSeries"}, {TimerIdRole, "timerId"},
            {SeriesTimerIdRole, "seriesTimerId"}};
  }

  void LiveTvSchedule::setChannelId(const QString& channelId) {
    if (channelId == m_channelId) return;
    m_channelId = channelId;
    m_channel = {};
    m_imageUrl = QUrl();
    Q_EMIT channelChanged();
    reload();
  }

  void LiveTvSchedule::reload() {
    const quint64 serial = ++m_serial;
    if (m_channelId.isEmpty()) {
      setPrograms({});
      return;
    }
    setLoading(true);
    setError({});
    m_api.channel(m_channelId, this, [this, serial](api::Result<api::LiveTvChannel> channel) {
      if (serial != m_serial) return;
      if (!channel) return fail(channel.error());
      m_channel = *channel;
      m_imageUrl = QUrl();
      if (!m_channel.primaryImageTag.isEmpty()) {
        api::ImageRequest image;
        image.itemId = m_channel.id;
        image.tag = m_channel.primaryImageTag;
        image.maxWidth = static_cast<int>(
            std::lround(m_options.imageWidth * std::max<qreal>(1.0, m_options.devicePixelRatio)));
        m_imageUrl = m_client.imageUrl(image);
      }
      Q_EMIT channelChanged();

      const QDateTime from = now();
      api::LiveTvProgramQuery query;
      query.channelIds = {m_channelId};
      query.from = from;
      query.to = from.addSecs(qint64(m_options.hours) * 3600);
      query.limit = m_options.programPageSize;
      m_api.allPrograms(query, this, [this, serial](api::Result<QList<api::LiveTvProgram>> programs) {
        if (serial != m_serial) return;
        if (!programs) return fail(programs.error());
        QList<api::LiveTvProgram> mine;
        for (const api::LiveTvProgram& p : *programs)
          if (p.channelId.isEmpty() || p.channelId == m_channelId) mine.append(p);
        // The guide may not cover now; the channel's own idea of it does.
        if (m_channel.currentProgram && m_channel.currentProgram->isOnAt(now()) &&
            std::none_of(mine.cbegin(), mine.cend(),
                         [&](const api::LiveTvProgram& p) { return p.id == m_channel.currentProgram->id; }))
          mine.prepend(*m_channel.currentProgram);
        setPrograms(std::move(mine));
        setLoading(false);
      });
    });
  }

  void LiveTvSchedule::setPrograms(QList<api::LiveTvProgram> programs) {
    const QDateTime at = now();
    programs.erase(std::remove_if(programs.begin(), programs.end(),
                                  [&](const api::LiveTvProgram& p) { return p.end <= at; }),
                   programs.end());
    std::stable_sort(programs.begin(), programs.end(),
                     [](const api::LiveTvProgram& a, const api::LiveTvProgram& b) { return a.start < b.start; });
    const bool countChanges = programs.size() != m_programs.size();
    beginResetModel();
    m_programs = std::move(programs);
    endResetModel();
    if (countChanges) Q_EMIT countChanged();
    if (m_programs.isEmpty()) m_tick.stop();
    else m_tick.start();
  }

  void LiveTvSchedule::applyTimers(const QList<api::LiveTvTimer>& timers) {
    QHash<QString, const api::LiveTvTimer*> byProgram;
    for (const api::LiveTvTimer& t : timers)
      if (!t.programId.isEmpty()) byProgram.insert(t.programId, &t);
    for (int row = 0; row < count(); ++row) {
      api::LiveTvProgram& p = m_programs[row];
      const api::LiveTvTimer* t = byProgram.value(p.id, nullptr);
      const QString timerId = t ? t->id : QString();
      const QString seriesTimerId = t ? t->seriesTimerId : QString();
      if (p.timerId == timerId && p.seriesTimerId == seriesTimerId) continue;
      p.timerId = timerId;
      p.seriesTimerId = seriesTimerId;
      Q_EMIT dataChanged(index(row), index(row), {TimerIdRole, SeriesTimerIdRole});
    }
  }

  void LiveTvSchedule::tick() {
    if (m_programs.isEmpty()) return;
    const QDateTime at = now();
    int ended = 0;
    while (ended < count() && m_programs[ended].end <= at) ++ended;
    if (ended > 0) {
      beginRemoveRows({}, 0, ended - 1);
      m_programs.erase(m_programs.begin(), m_programs.begin() + ended);
      endRemoveRows();
      Q_EMIT countChanged();
      // Running out: load the next hours.
      if (m_programs.size() < 2) reload();
    }
    if (!m_programs.isEmpty())
      Q_EMIT dataChanged(index(0), index(count() - 1), {OnNowRole, ProgressRole});
  }

  void LiveTvSchedule::fail(const api::ApiError& error) {
    setLoading(false);
    setError(library::errorText(error));
  }

  void LiveTvSchedule::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void LiveTvSchedule::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

} // namespace jfd::library
