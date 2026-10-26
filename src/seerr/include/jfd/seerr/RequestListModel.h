#pragma once

#include <jfd/seerr/Models.h>
#include <jfd/seerr/SeerrService.h>

#include <QAbstractListModel>
#include <QList>

namespace jfd::seerr {

  // The signed-in user's own requests, newest first, with each title's name
  // and poster (Seerr lists a request with the TMDB id only, so the titles
  // are looked up one by one and shown as they arrive).
  class RequestListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(int error READ error NOTIFY errorChanged)

  public:
    enum Role {
      RequestIdRole = Qt::UserRole + 1,
      KeyRole,
      MediaTypeRole,
      NameRole,
      // The production year, or empty.
      SubtitleRole,
      ImageUrlRole,
      // A RequestStatus and a MediaStatus.
      RequestStatusRole,
      MediaStatusRole,
      // "Seasons 1, 2"-style numbers for a series: the list of numbers.
      SeasonsRole,
      CanCancelRole,
    };
    Q_ENUM(Role)

    explicit RequestListModel(SeerrService& service, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    bool loading() const { return m_loading; }
    bool hasMore() const { return int(m_rows.size()) < m_total; }
    int error() const { return m_error; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    // Cancels a request that is still waiting for approval, then reloads.
    Q_INVOKABLE void cancel(int row);

  Q_SIGNALS:
    void countChanged();
    void loadingChanged();
    void hasMoreChanged();
    void errorChanged();
    // The Outcome of cancel().
    void cancelFinished(int outcome);

  private:
    struct Row {
      UserRequest request;
      Title title;
      bool titled = false;
    };

    void fetch(int skip);
    void apply(quint64 generation, int skip, api::Result<RequestPage> result);
    void lookUp(quint64 generation, const UserRequest& request);
    void setLoading(bool loading);
    void setError(int error);

    SeerrService& m_service;
    QList<Row> m_rows;
    int m_total = 0;
    bool m_loading = false;
    int m_error = 0;
    quint64 m_generation = 0;
  };

} // namespace jfd::seerr
