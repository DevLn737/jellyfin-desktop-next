#pragma once

#include <jfd/seerr/Models.h>
#include <jfd/seerr/SeerrService.h>

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

namespace jfd::seerr {

  // One title's page: the facts, the availability and the requests made for
  // it, and the "Request" and "Cancel my request" actions.
  class TitleDetailsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    // An Outcome; 0 while there is no error.
    Q_PROPERTY(int error READ error NOTIFY changed)
    Q_PROPERTY(QString mediaType READ mediaType NOTIFY changed)
    Q_PROPERTY(int tmdbId READ tmdbId NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString tagline READ tagline NOTIFY changed)
    Q_PROPERTY(QString overview READ overview NOTIFY changed)
    Q_PROPERTY(QString year READ year NOTIFY changed)
    Q_PROPERTY(int runtimeMinutes READ runtimeMinutes NOTIFY changed)
    Q_PROPERTY(QStringList genres READ genres NOTIFY changed)
    Q_PROPERTY(double rating READ rating NOTIFY changed)
    Q_PROPERTY(QUrl posterUrl READ posterUrl NOTIFY changed)
    Q_PROPERTY(QUrl backdropUrl READ backdropUrl NOTIFY changed)
    // A MediaStatus.
    Q_PROPERTY(int status READ status NOTIFY changed)
    // Series only: maps with number, name, episodeCount, status, requestable.
    Q_PROPERTY(QVariantList seasons READ seasons NOTIFY changed)
    // The signed-in user's open request for this title: its id and a
    // RequestStatus; 0 when there is none.
    Q_PROPERTY(int myRequestId READ myRequestId NOTIFY changed)
    Q_PROPERTY(int myRequestStatus READ myRequestStatus NOTIFY changed)
    // The account may request and there is something left to request.
    Q_PROPERTY(bool canRequest READ canRequest NOTIFY changed)
    // The account may not make requests at all.
    Q_PROPERTY(bool requestsNotAllowed READ requestsNotAllowed NOTIFY changed)
    Q_PROPERTY(bool working READ working NOTIFY changed)

  public:
    explicit TitleDetailsModel(SeerrService& service, QObject* parent = nullptr);

    bool loaded() const { return m_loaded; }
    bool loading() const { return m_loading; }
    int error() const { return m_error; }
    QString mediaType() const;
    int tmdbId() const { return m_details.title.tmdbId; }
    QString name() const { return m_details.title.title; }
    QString tagline() const { return m_details.tagline; }
    QString overview() const { return m_details.title.overview; }
    QString year() const;
    int runtimeMinutes() const { return m_details.runtimeMinutes; }
    QStringList genres() const { return m_details.genres; }
    double rating() const { return m_details.title.voteAverage; }
    QUrl posterUrl() const;
    QUrl backdropUrl() const;
    int status() const { return int(m_details.title.status); }
    QVariantList seasons() const;
    int myRequestId() const;
    int myRequestStatus() const;
    bool canRequest() const;
    bool requestsNotAllowed() const;
    bool working() const { return m_working; }

    // "movie:603" or "tv:1399"; false for anything else.
    Q_INVOKABLE bool load(const QString& key);
    Q_INVOKABLE void reload();
    // A movie: `seasons` is ignored. A series: all seasons, or the listed
    // numbers.
    Q_INVOKABLE void request(bool allSeasons, const QVariantList& seasons);
    Q_INVOKABLE void cancelMine();

    static bool parseKey(const QString& key, MediaType* type, int* tmdbId);

  Q_SIGNALS:
    void changed();
    // The Outcome of request() or cancelMine().
    void actionFinished(int outcome);

  private:
    void apply(quint64 generation, api::Result<TitleDetails> result);
    void finishAction(Outcome outcome);

    SeerrService& m_service;
    MediaType m_type = MediaType::Movie;
    int m_id = 0;
    TitleDetails m_details;
    bool m_loaded = false;
    bool m_loading = false;
    bool m_working = false;
    int m_error = 0;
    quint64 m_generation = 0;
  };

} // namespace jfd::seerr
