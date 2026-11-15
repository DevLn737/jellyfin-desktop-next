#include <jfd/library/LiveTvRecordings.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/ErrorText.h>

#include <algorithm>
#include <cmath>

namespace jfd::library {

  namespace {
    QDateTime dateOf(const api::LiveTvRecording& r) {
      return r.start.isValid() ? r.start : r.dateCreated;
    }

    bool isEpisode(const api::LiveTvRecording& r) {
      return !r.seriesName.isEmpty() && r.seriesName != r.name;
    }
  } // namespace

  LiveTvRecordings::LiveTvRecordings(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_api(client), m_options(options) {
    m_options.pageSize = std::max(1, m_options.pageSize);
  }

  int LiveTvRecordings::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant LiveTvRecordings::data(const QModelIndex& index, int role) const {
    if (index.parent().isValid() || index.row() < 0 || index.row() >= count())
      return {};
    const api::LiveTvRecording& r = m_rows[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
      return isEpisode(r) ? r.seriesName : r.name;
    case IdRole:
      return r.id;
    case SubtitleRole:
      return isEpisode(r) ? r.name : QString();
    case OverviewRole:
      return r.overview;
    case ChannelNameRole:
      return r.channelName;
    case DateRole:
      return dateOf(r).toLocalTime();
    case RunTimeTicksRole:
      return r.runTimeTicks;
    case PositionTicksRole:
      return r.playbackPositionTicks;
    case PlayedRole:
      return r.played;
    case InProgressRole:
      return r.isInProgress();
    case TimerIdRole:
      return r.timerId;
    case ImageUrlRole: {
      if (r.primaryImageTag.isEmpty())
        return QUrl();
      api::ImageRequest image;
      image.itemId = r.id;
      image.tag = r.primaryImageTag;
      image.maxWidth = static_cast<int>(
          std::lround(m_options.imageWidth * std::max<qreal>(1.0, m_options.devicePixelRatio)));
      return m_client.imageUrl(image);
    }
    default:
      return {};
    }
  }

  QHash<int, QByteArray> LiveTvRecordings::roleNames() const {
    return {{IdRole, "recordingId"},
            {TitleRole, "title"},
            {SubtitleRole, "subtitle"},
            {OverviewRole, "overview"},
            {ChannelNameRole, "channelName"},
            {DateRole, "date"},
            {RunTimeTicksRole, "runTimeTicks"},
            {PositionTicksRole, "positionTicks"},
            {PlayedRole, "played"},
            {InProgressRole, "inProgress"},
            {TimerIdRole, "timerId"},
            {ImageUrlRole, "imageUrl"}};
  }

  void LiveTvRecordings::reload() {
    const quint64 serial = ++m_serial;
    setLoading(true);
    setError({});
    loadPage(serial, 0, {});
  }

  void LiveTvRecordings::loadPage(quint64 serial, int startIndex, QList<api::LiveTvRecording> got) {
    api::LiveTvRecordingQuery query;
    query.startIndex = startIndex;
    query.limit = m_options.pageSize;
    m_api.recordings(
        query, this,
        [this, serial, got = std::move(got)](api::Result<api::LiveTvRecordingPage> page) mutable {
          if (serial != m_serial)
            return;
          if (!page) {
            setLoading(false);
            return setError(library::errorText(page.error()));
          }
          got.append(page->recordings);
          const int next = page->startIndex + static_cast<int>(page->recordings.size());
          if (page->recordings.isEmpty() || next >= page->totalRecordCount) {
            setRows(std::move(got));
            return setLoading(false);
          }
          loadPage(serial, next, std::move(got));
        });
  }

  void LiveTvRecordings::setRows(QList<api::LiveTvRecording> rows) {
    // Recording now first, then newest first.
    std::stable_sort(rows.begin(), rows.end(),
                     [](const api::LiveTvRecording& a, const api::LiveTvRecording& b) {
                       if (a.isInProgress() != b.isInProgress())
                         return a.isInProgress();
                       return dateOf(a) > dateOf(b);
                     });
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    if (countChanges)
      Q_EMIT countChanged();
  }

  void LiveTvRecordings::remove(const QString& recordingId) {
    if (recordingId.isEmpty())
      return;
    m_api.deleteRecording(recordingId, this, [this, recordingId](api::Result<api::Empty> result) {
      if (!result) {
        Q_EMIT failed(
            result.error().isForbidden()
                ? tr("The server does not allow you to delete recordings.")
                : tr("Could not delete the recording: %1").arg(library::errorText(result.error())));
        return;
      }
      const auto at =
          std::find_if(m_rows.cbegin(), m_rows.cend(),
                       [&](const api::LiveTvRecording& r) { return r.id == recordingId; });
      if (at != m_rows.cend()) {
        const int row = static_cast<int>(at - m_rows.cbegin());
        beginRemoveRows({}, row, row);
        m_rows.removeAt(row);
        endRemoveRows();
        Q_EMIT countChanged();
      }
      Q_EMIT removed(recordingId);
    });
  }

  void LiveTvRecordings::setLoading(bool loading) {
    if (m_loading == loading)
      return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void LiveTvRecordings::setError(const QString& text) {
    if (m_errorText == text)
      return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

} // namespace jfd::library
