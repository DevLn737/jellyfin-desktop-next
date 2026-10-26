#include <jfd/seerr/RequestListModel.h>

namespace jfd::seerr {

  namespace {
    constexpr int PageSize = 20;
  }

  RequestListModel::RequestListModel(SeerrService& service, QObject* parent)
      : QAbstractListModel(parent), m_service(service) {}

  int RequestListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  QVariant RequestListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const Row& row = m_rows[index.row()];
    switch (role) {
    case RequestIdRole: return row.request.id;
    case KeyRole:
      return QStringLiteral("%1:%2")
          .arg(row.request.type == MediaType::Movie ? QStringLiteral("movie") : QStringLiteral("tv"))
          .arg(row.request.tmdbId);
    case MediaTypeRole:
      return row.request.type == MediaType::Movie ? QStringLiteral("movie") : QStringLiteral("tv");
    case Qt::DisplayRole:
    case NameRole: return row.titled ? row.title.title : QString();
    case SubtitleRole:
      return row.titled && row.title.year() > 0 ? QString::number(row.title.year()) : QString();
    case ImageUrlRole: return row.titled ? SeerrService::imageUrl(row.title.posterPath) : QUrl();
    case RequestStatusRole: return int(row.request.status);
    case MediaStatusRole: return int(row.request.mediaStatus);
    case SeasonsRole: {
      QVariantList numbers;
      for (int n : row.request.seasons) numbers.append(n);
      return numbers;
    }
    case CanCancelRole: return row.request.status == RequestStatus::PendingApproval;
    default: return {};
    }
  }

  QHash<int, QByteArray> RequestListModel::roleNames() const {
    return {{RequestIdRole, "requestId"},         {KeyRole, "key"},
            {MediaTypeRole, "mediaType"},         {NameRole, "name"},
            {SubtitleRole, "subtitle"},           {ImageUrlRole, "imageUrl"},
            {RequestStatusRole, "requestStatus"}, {MediaStatusRole, "mediaStatus"},
            {SeasonsRole, "seasons"},             {CanCancelRole, "canCancel"}};
  }

  void RequestListModel::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void RequestListModel::setError(int error) {
    if (m_error == error) return;
    m_error = error;
    Q_EMIT errorChanged();
  }

  void RequestListModel::reload() {
    ++m_generation;
    fetch(0);
  }

  void RequestListModel::loadMore() {
    if (m_loading || !hasMore()) return;
    fetch(count());
  }

  void RequestListModel::fetch(int skip) {
    if (!m_service.signedIn()) {
      setError(int(Outcome::SignedOut));
      return;
    }
    setLoading(true);
    const quint64 generation = m_generation;
    m_service.client().myRequests(PageSize, skip, this, [this, generation, skip](api::Result<RequestPage> r) {
      apply(generation, skip, std::move(r));
    });
  }

  void RequestListModel::apply(quint64 generation, int skip, api::Result<RequestPage> result) {
    if (generation != m_generation) return;
    setLoading(false);
    if (!result.ok()) {
      setError(int(outcomeOf(refusalOf(result.error()))));
      return;
    }
    setError(0);
    m_total = result->total;
    QList<Row> fresh;
    for (const UserRequest& request : result->results) {
      Row row;
      row.request = request;
      // A title seen before keeps its name while the page reloads.
      for (const Row& old : std::as_const(m_rows)) {
        if (old.titled && old.request.tmdbId == request.tmdbId && old.request.type == request.type) {
          row.title = old.title;
          row.titled = true;
        }
      }
      fresh.append(row);
    }
    if (skip == 0) {
      beginResetModel();
      m_rows = fresh;
      endResetModel();
    } else if (!fresh.isEmpty()) {
      beginInsertRows({}, count(), count() + int(fresh.size()) - 1);
      m_rows += fresh;
      endInsertRows();
    }
    Q_EMIT countChanged();
    Q_EMIT hasMoreChanged();
    for (const Row& row : std::as_const(fresh)) {
      if (!row.titled) lookUp(generation, row.request);
    }
  }

  void RequestListModel::lookUp(quint64 generation, const UserRequest& request) {
    auto done = [this, generation, request](api::Result<TitleDetails> details) {
      if (generation != m_generation || !details.ok()) return;
      for (int i = 0; i < count(); ++i) {
        Row& row = m_rows[i];
        if (row.request.id != request.id) continue;
        row.title = details->title;
        row.titled = true;
        Q_EMIT dataChanged(index(i), index(i));
      }
    };
    if (request.type == MediaType::Movie) m_service.client().movie(request.tmdbId, this, done);
    else m_service.client().tv(request.tmdbId, this, done);
  }

  void RequestListModel::cancel(int row) {
    if (row < 0 || row >= count() || m_rows[row].request.status != RequestStatus::PendingApproval)
      return;
    m_service.client().cancelRequest(m_rows[row].request.id, this, [this](api::Result<api::Empty> r) {
      Q_EMIT cancelFinished(int(r.ok() ? Outcome::Ok : outcomeOf(refusalOf(r.error()))));
      reload();
    });
  }

} // namespace jfd::seerr
