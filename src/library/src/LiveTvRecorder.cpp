#include <jfd/library/LiveTvRecorder.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ErrorText.h>

#include <algorithm>

namespace jfd::library {

  LiveTvRecorder::LiveTvRecorder(api::JellyfinClient& client, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_api(client) {}

  int LiveTvRecorder::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant LiveTvRecorder::data(const QModelIndex& index, int role) const {
    if (index.parent().isValid() || index.row() < 0 || index.row() >= count())
      return {};
    const bool series = index.row() < m_series.size();
    const api::LiveTvTimer& t =
        series ? m_series[index.row()] : m_timers[index.row() - m_series.size()];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
      return t.name;
    case IdRole:
      return t.id;
    case IsSeriesRole:
      return series;
    case OverviewRole:
      return t.overview;
    case ChannelNameRole:
      return t.channelName;
    case StartRole:
      return series ? QDateTime() : t.start.toLocalTime();
    case EndRole:
      return series ? QDateTime() : t.end.toLocalTime();
    case RecordingRole:
      return !series && t.isRecording();
    case SeriesTimerIdRole:
      return series ? t.id : t.seriesTimerId;
    default:
      return {};
    }
  }

  QHash<int, QByteArray> LiveTvRecorder::roleNames() const {
    return {{IdRole, "timerId"},
            {IsSeriesRole, "isSeries"},
            {TitleRole, "title"},
            {OverviewRole, "overview"},
            {ChannelNameRole, "channelName"},
            {StartRole, "start"},
            {EndRole, "end"},
            {RecordingRole, "recording"},
            {SeriesTimerIdRole, "seriesTimerId"}};
  }

  QString LiveTvRecorder::timerFor(const QString& programId) const {
    if (programId.isEmpty())
      return {};
    for (const api::LiveTvTimer& t : m_timers)
      if (t.programId == programId)
        return t.id;
    return {};
  }

  void LiveTvRecorder::checkAvailable() {
    const quint64 serial = ++m_infoSerial;
    m_api.info(this, [this, serial](api::Result<api::LiveTvInfo> info) {
      if (serial != m_infoSerial)
        return;
      if (!info || !info->availableTo(m_client.userId()))
        return setAvailable(false);
      m_client.currentUser(this, [this, serial](api::Result<api::UserInfo> user) {
        if (serial != m_infoSerial)
          return;
        setAvailable(user && (user->isAdministrator || user->enableLiveTvManagement));
        if (m_available)
          reload();
      });
    });
  }

  void LiveTvRecorder::reload() {
    const quint64 serial = ++m_serial;
    setLoading(true);
    setError({});
    m_api.seriesTimers(this, [this, serial](api::Result<QList<api::LiveTvTimer>> series) {
      if (serial != m_serial)
        return;
      if (!series) {
        setLoading(false);
        return setError(library::errorText(series.error()));
      }
      m_api.timers({}, this,
                   [this, serial, series = *series](api::Result<QList<api::LiveTvTimer>> timers) {
                     if (serial != m_serial)
                       return;
                     setLoading(false);
                     if (!timers)
                       return setError(library::errorText(timers.error()));
                     setTimers(series, *timers);
                   });
    });
  }

  void LiveTvRecorder::setTimers(QList<api::LiveTvTimer> series, QList<api::LiveTvTimer> timers) {
    // Recordings that ended or were cancelled are the recordings list's.
    timers.erase(std::remove_if(timers.begin(), timers.end(),
                                [](const api::LiveTvTimer& t) { return !t.isScheduled(); }),
                 timers.end());
    std::stable_sort(
        timers.begin(), timers.end(),
        [](const api::LiveTvTimer& a, const api::LiveTvTimer& b) { return a.start < b.start; });
    std::stable_sort(series.begin(), series.end(),
                     [](const api::LiveTvTimer& a, const api::LiveTvTimer& b) {
                       return a.name.localeAwareCompare(b.name) < 0;
                     });
    const int before = count();
    beginResetModel();
    m_series = std::move(series);
    m_timers = std::move(timers);
    endResetModel();
    if (count() != before)
      Q_EMIT countChanged();
    Q_EMIT timersChanged();
  }

  void LiveTvRecorder::record(const QString& programId) {
    if (programId.isEmpty())
      return;
    setBusy(1);
    const quint64 session = m_session;
    m_api.recordProgram(programId, this, [this, session](api::Result<api::Empty> result) {
      if (session != m_session)
        return;
      finished(Action::Record, result);
    });
  }

  void LiveTvRecorder::recordSeries(const QString& programId) {
    if (programId.isEmpty())
      return;
    setBusy(1);
    const quint64 session = m_session;
    m_api.recordSeries(programId, this, [this, session](api::Result<api::Empty> result) {
      if (session != m_session)
        return;
      finished(Action::RecordSeries, result);
    });
  }

  void LiveTvRecorder::cancel(const QString& timerId) {
    if (timerId.isEmpty())
      return;
    setBusy(1);
    const quint64 session = m_session;
    m_api.cancelTimer(timerId, this, [this, session](api::Result<api::Empty> result) {
      if (session != m_session)
        return;
      finished(Action::Cancel, result);
    });
  }

  void LiveTvRecorder::cancelSeries(const QString& seriesTimerId) {
    if (seriesTimerId.isEmpty())
      return;
    setBusy(1);
    const quint64 session = m_session;
    m_api.cancelSeriesTimer(seriesTimerId, this, [this, session](api::Result<api::Empty> result) {
      if (session != m_session)
        return;
      finished(Action::CancelSeries, result);
    });
  }

  void LiveTvRecorder::finished(Action action, const api::Result<api::Empty>& result) {
    setBusy(-1);
    if (!result) {
      const api::ApiError& error = result.error();
      Q_EMIT message(error.isForbidden() ? tr("The server does not allow you to record.")
                     : action == Action::Record || action == Action::RecordSeries
                         ? tr("Could not schedule the recording: %1").arg(library::errorText(error))
                         : tr("Could not cancel the recording: %1").arg(library::errorText(error)),
                     true);
      // What the server has now, whatever it did.
      reload();
      return;
    }
    switch (action) {
    case Action::Record:
      Q_EMIT message(tr("Recording scheduled."), false);
      break;
    case Action::RecordSeries:
      Q_EMIT message(tr("Series recording scheduled."), false);
      break;
    case Action::Cancel:
      Q_EMIT message(tr("Recording cancelled."), false);
      break;
    case Action::CancelSeries:
      Q_EMIT message(tr("Series recording cancelled."), false);
      break;
    }
    reload();
  }

  void LiveTvRecorder::clear() {
    ++m_serial;
    ++m_infoSerial;
    ++m_session;
    const int before = count();
    beginResetModel();
    m_series.clear();
    m_timers.clear();
    endResetModel();
    if (before != 0)
      Q_EMIT countChanged();
    if (m_busy != 0) {
      m_busy = 0;
      Q_EMIT busyChanged();
    }
    setAvailable(false);
    setLoading(false);
    setError({});
    Q_EMIT timersChanged();
  }

  void LiveTvRecorder::setAvailable(bool available) {
    if (m_available == available)
      return;
    m_available = available;
    Q_EMIT availableChanged();
  }

  void LiveTvRecorder::setLoading(bool loading) {
    if (m_loading == loading)
      return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void LiveTvRecorder::setError(const QString& text) {
    if (m_errorText == text)
      return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

  void LiveTvRecorder::setBusy(int delta) {
    const bool was = busy();
    m_busy = std::max(0, m_busy + delta);
    if (busy() != was)
      Q_EMIT busyChanged();
  }

} // namespace jfd::library
