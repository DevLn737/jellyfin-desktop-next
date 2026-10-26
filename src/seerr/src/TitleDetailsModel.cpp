#include <jfd/seerr/TitleDetailsModel.h>

#include <QVariantMap>

namespace jfd::seerr {

  namespace {
    bool seasonRequestable(MediaStatus status) {
      return status == MediaStatus::None || status == MediaStatus::Unknown ||
             status == MediaStatus::Deleted;
    }
  } // namespace

  TitleDetailsModel::TitleDetailsModel(SeerrService& service, QObject* parent)
      : QObject(parent), m_service(service) {
    // Signing out or in changes what this page may do.
    connect(&service, &SeerrService::stateChanged, this, &TitleDetailsModel::changed);
  }

  bool TitleDetailsModel::parseKey(const QString& key, MediaType* type, int* tmdbId) {
    const qsizetype colon = key.indexOf(QLatin1Char(':'));
    if (colon <= 0) return false;
    const QString kind = key.left(colon);
    bool ok = false;
    const int id = key.mid(colon + 1).toInt(&ok);
    if (!ok || id <= 0) return false;
    if (kind == QLatin1String("movie")) *type = MediaType::Movie;
    else if (kind == QLatin1String("tv")) *type = MediaType::Tv;
    else return false;
    *tmdbId = id;
    return true;
  }

  QString TitleDetailsModel::mediaType() const {
    return m_details.title.type == MediaType::Movie ? QStringLiteral("movie") : QStringLiteral("tv");
  }

  QString TitleDetailsModel::year() const {
    const int year = m_details.title.year();
    return year > 0 ? QString::number(year) : QString();
  }

  QUrl TitleDetailsModel::posterUrl() const {
    return SeerrService::imageUrl(m_details.title.posterPath, 500);
  }

  QUrl TitleDetailsModel::backdropUrl() const {
    return SeerrService::imageUrl(m_details.title.backdropPath, 1280);
  }

  QVariantList TitleDetailsModel::seasons() const {
    QVariantList out;
    for (const Season& s : m_details.seasons) {
      out.append(QVariantMap{{QStringLiteral("number"), s.number},
                             {QStringLiteral("name"), s.name},
                             {QStringLiteral("episodeCount"), s.episodeCount},
                             {QStringLiteral("status"), int(s.status)},
                             {QStringLiteral("requestable"), seasonRequestable(s.status)}});
    }
    return out;
  }

  int TitleDetailsModel::myRequestId() const {
    for (const UserRequest& r : m_details.requests) {
      if (r.requestedBy == m_service.userId() && r.status != RequestStatus::Declined) return r.id;
    }
    return 0;
  }

  int TitleDetailsModel::myRequestStatus() const {
    for (const UserRequest& r : m_details.requests) {
      if (r.requestedBy == m_service.userId() && r.status != RequestStatus::Declined)
        return int(r.status);
    }
    return 0;
  }

  bool TitleDetailsModel::requestsNotAllowed() const {
    return m_loaded && m_service.signedIn() && !m_service.mayRequest();
  }

  bool TitleDetailsModel::canRequest() const {
    if (!m_loaded || !m_service.signedIn() || !m_service.mayRequest()) return false;
    if (m_details.title.type == MediaType::Movie) return m_details.title.canRequest();
    if (m_details.seasons.isEmpty()) return m_details.title.canRequest();
    for (const Season& s : m_details.seasons) {
      if (seasonRequestable(s.status)) return true;
    }
    return false;
  }

  bool TitleDetailsModel::load(const QString& key) {
    if (!parseKey(key, &m_type, &m_id)) return false;
    m_details = {};
    m_loaded = false;
    reload();
    return true;
  }

  void TitleDetailsModel::reload() {
    if (m_id <= 0) return;
    if (!m_service.signedIn()) {
      m_error = int(Outcome::SignedOut);
      Q_EMIT changed();
      return;
    }
    const quint64 generation = ++m_generation;
    m_loading = true;
    Q_EMIT changed();
    auto done = [this, generation](api::Result<TitleDetails> result) {
      apply(generation, std::move(result));
    };
    if (m_type == MediaType::Movie) m_service.client().movie(m_id, this, done);
    else m_service.client().tv(m_id, this, done);
  }

  void TitleDetailsModel::apply(quint64 generation, api::Result<TitleDetails> result) {
    if (generation != m_generation) return;
    m_loading = false;
    if (!result.ok()) {
      m_error = int(outcomeOf(refusalOf(result.error())));
    } else {
      m_error = 0;
      m_details = std::move(result).value();
      m_loaded = true;
    }
    Q_EMIT changed();
  }

  void TitleDetailsModel::finishAction(Outcome outcome) {
    m_working = false;
    Q_EMIT changed();
    Q_EMIT actionFinished(int(outcome));
    // Whatever the answer, the page shows what Seerr says now.
    reload();
  }

  void TitleDetailsModel::request(bool allSeasons, const QVariantList& seasons) {
    if (!m_loaded || m_working) return;
    SeasonSelection selection;
    selection.all = allSeasons || m_type == MediaType::Movie;
    if (!selection.all) {
      for (const QVariant& s : seasons) selection.numbers.append(s.toInt());
      if (selection.numbers.isEmpty()) return;
    }
    m_working = true;
    Q_EMIT changed();
    m_service.client().request(m_type, m_id, selection, this, [this](api::Result<UserRequest> r) {
      finishAction(r.ok() ? Outcome::Ok : outcomeOf(refusalOf(r.error())));
    });
  }

  void TitleDetailsModel::cancelMine() {
    const int id = myRequestId();
    if (id <= 0 || m_working) return;
    m_working = true;
    Q_EMIT changed();
    m_service.client().cancelRequest(id, this, [this](api::Result<api::Empty> r) {
      finishAction(r.ok() ? Outcome::Ok : outcomeOf(refusalOf(r.error())));
    });
  }

} // namespace jfd::seerr
