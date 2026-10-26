#pragma once

#include <jfd/seerr/Models.h>
#include <jfd/seerr/SeerrService.h>

#include <QAbstractListModel>
#include <QList>

namespace jfd::seerr {

  // A list of titles from Seerr for a row or a grid: what is popular, what
  // is trending, or the result of a search. Pages are added with
  // loadMore(); an answer to an earlier source or search is dropped.
  class TitleListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    // An Outcome; Ok while there is no error.
    Q_PROPERTY(int error READ error NOTIFY errorChanged)

  public:
    enum class Source { PopularMovies, PopularTv, Trending, Search };
    Q_ENUM(Source)

    enum Role {
      // "movie:603": what the item route takes.
      KeyRole = Qt::UserRole + 1,
      MediaTypeRole,
      TmdbIdRole,
      NameRole,
      // The production year, or empty.
      SubtitleRole,
      ImageUrlRole,
      OverviewRole,
      RatingRole,
      // A MediaStatus.
      StatusRole,
      CanRequestRole,
    };
    Q_ENUM(Role)

    TitleListModel(SeerrService& service, Source source, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_titles.size()); }
    bool loading() const { return m_loading; }
    bool hasMore() const { return m_page < m_totalPages; }
    int error() const { return m_error; }
    const Title* title(int row) const;

    // Starts again from the first page, keeping the rows until the answer.
    Q_INVOKABLE void reload();
    Q_INVOKABLE void loadMore();
    // Search only: a new text; an empty text empties the list.
    Q_INVOKABLE void search(const QString& text);

    static QString keyOf(const Title& title);

  Q_SIGNALS:
    void countChanged();
    void loadingChanged();
    void hasMoreChanged();
    void errorChanged();

  private:
    void fetch(int page);
    void setLoading(bool loading);
    void setError(int error);
    void apply(quint64 generation, int page, api::Result<TitlePage> result);

    SeerrService& m_service;
    Source m_source;
    QString m_text;
    QList<Title> m_titles;
    int m_page = 0;
    int m_totalPages = 1;
    bool m_loading = false;
    int m_error = 0;
    quint64 m_generation = 0;
  };

} // namespace jfd::seerr
