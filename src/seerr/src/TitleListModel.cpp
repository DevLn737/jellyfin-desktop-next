#include <jfd/seerr/TitleListModel.h>

namespace jfd::seerr {

  TitleListModel::TitleListModel(SeerrService& service, Source source, QObject* parent)
      : QAbstractListModel(parent), m_service(service), m_source(source) {}

  int TitleListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
  }

  const Title* TitleListModel::title(int row) const {
    return row >= 0 && row < count() ? &m_titles[row] : nullptr;
  }

  QString TitleListModel::keyOf(const Title& title) {
    return QStringLiteral("%1:%2")
        .arg(title.type == MediaType::Movie ? QStringLiteral("movie") : QStringLiteral("tv"))
        .arg(title.tmdbId);
  }

  QVariant TitleListModel::data(const QModelIndex& index, int role) const {
    const Title* t = index.isValid() ? title(index.row()) : nullptr;
    if (t == nullptr) return {};
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return t->title;
    case KeyRole: return keyOf(*t);
    case MediaTypeRole: return t->type == MediaType::Movie ? QStringLiteral("movie") : QStringLiteral("tv");
    case TmdbIdRole: return t->tmdbId;
    case SubtitleRole: return t->year() > 0 ? QVariant(QString::number(t->year())) : QVariant(QString());
    case ImageUrlRole: return SeerrService::imageUrl(t->posterPath);
    case OverviewRole: return t->overview;
    case RatingRole: return t->voteAverage;
    case StatusRole: return int(t->status);
    case CanRequestRole: return t->canRequest();
    default: return {};
    }
  }

  QHash<int, QByteArray> TitleListModel::roleNames() const {
    return {{KeyRole, "key"},           {MediaTypeRole, "mediaType"}, {TmdbIdRole, "tmdbId"},
            {NameRole, "name"},         {SubtitleRole, "subtitle"},   {ImageUrlRole, "imageUrl"},
            {OverviewRole, "overview"}, {RatingRole, "rating"},       {StatusRole, "status"},
            {CanRequestRole, "canRequest"}};
  }

  void TitleListModel::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    Q_EMIT loadingChanged();
  }

  void TitleListModel::setError(int error) {
    if (m_error == error) return;
    m_error = error;
    Q_EMIT errorChanged();
  }

  void TitleListModel::reload() {
    ++m_generation;
    m_page = 0;
    m_totalPages = 1;
    if (m_source == Source::Search && m_text.trimmed().isEmpty()) {
      beginResetModel();
      m_titles.clear();
      endResetModel();
      Q_EMIT countChanged();
      Q_EMIT hasMoreChanged();
      setLoading(false);
      setError(0);
      return;
    }
    fetch(1);
  }

  void TitleListModel::search(const QString& text) {
    if (m_source != Source::Search) return;
    m_text = text;
    reload();
  }

  void TitleListModel::loadMore() {
    if (m_loading || !hasMore()) return;
    fetch(m_page + 1);
  }

  void TitleListModel::fetch(int page) {
    if (!m_service.signedIn()) {
      setError(int(Outcome::SignedOut));
      return;
    }
    setLoading(true);
    const quint64 generation = m_generation;
    auto done = [this, generation, page](api::Result<TitlePage> result) {
      apply(generation, page, std::move(result));
    };
    SeerrClient& client = m_service.client();
    switch (m_source) {
    case Source::PopularMovies: client.discoverMovies(page, this, done); break;
    case Source::PopularTv: client.discoverTv(page, this, done); break;
    case Source::Trending: client.trending(page, this, done); break;
    case Source::Search: client.search(m_text.trimmed(), page, this, done); break;
    }
  }

  void TitleListModel::apply(quint64 generation, int page, api::Result<TitlePage> result) {
    if (generation != m_generation) return;
    setLoading(false);
    if (!result.ok()) {
      setError(int(outcomeOf(refusalOf(result.error()))));
      return;
    }
    setError(0);
    m_page = page;
    m_totalPages = result->totalPages;
    QList<Title> fresh;
    for (const Title& t : result->results) {
      bool seen = false;
      for (const Title& old : m_titles) seen = seen || (old.type == t.type && old.tmdbId == t.tmdbId);
      for (const Title& added : fresh) seen = seen || (added.type == t.type && added.tmdbId == t.tmdbId);
      if (!seen) fresh.append(t);
    }
    if (page == 1) {
      beginResetModel();
      m_titles = fresh;
      endResetModel();
    } else if (!fresh.isEmpty()) {
      beginInsertRows({}, count(), count() + int(fresh.size()) - 1);
      m_titles += fresh;
      endInsertRows();
    }
    Q_EMIT countChanged();
    Q_EMIT hasMoreChanged();
  }

} // namespace jfd::seerr
