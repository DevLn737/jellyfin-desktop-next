#pragma once

#include <jfd/library/CardImages.h>
#include <jfd/library/ItemListModel.h>

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QVariantList>

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::library {

  class LibraryUpdates;

  // One library as a grid: every item has a row from the start (the count
  // comes with the first page), and pages of items load when their rows are
  // first asked for, so the scroll bar is right at once and jumping to the
  // end loads only the end. Rows not yet loaded report `loaded` false.
  //
  // Changing the sort or a filter starts over; pages from the previous
  // listing are dropped when they arrive.
  class LibraryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString libraryId READ libraryId WRITE setLibraryId NOTIFY queryChanged)
    Q_PROPERTY(
        QString collectionType READ collectionType WRITE setCollectionType NOTIFY queryChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY queryChanged)
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY queryChanged)
    Q_PROPERTY(
        PlayedFilter playedFilter READ playedFilter WRITE setPlayedFilter NOTIFY queryChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY queryChanged)
    Q_PROPERTY(QStringList genres READ genres WRITE setGenres NOTIFY queryChanged)
    Q_PROPERTY(QVariantList years READ years WRITE setYears NOTIFY queryChanged)
    Q_PROPERTY(
        bool showCollections READ showCollections WRITE setShowCollections NOTIFY queryChanged)
    Q_PROPERTY(bool canShowCollections READ canShowCollections NOTIFY queryChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QStringList availableGenres READ availableGenres NOTIFY filtersChanged)
    Q_PROPERTY(QVariantList availableYears READ availableYears NOTIFY filtersChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

  public:
    enum class PlayedFilter { All, Played, Unplayed };
    Q_ENUM(PlayedFilter)

    enum Role { LoadedRole = ItemListModel::CommunityRatingRole + 1 };

    struct Options {
      int pageSize = 100;
      int cardWidth = 200;
      qreal devicePixelRatio = 1.0;
    };

    LibraryModel(api::JellyfinClient& client, Options options, QObject* parent = nullptr);
    explicit LibraryModel(api::JellyfinClient& client, QObject* parent = nullptr)
        : LibraryModel(client, Options{}, parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString libraryId() const { return m_libraryId; }
    void setLibraryId(const QString& id);
    // The library's CollectionType; decides which item types are listed.
    // CollectionItems lists the items of one collection (a BoxSet) instead.
    QString collectionType() const { return m_collectionType; }
    void setCollectionType(const QString& type);
    QString sortBy() const { return m_sortBy; }
    void setSortBy(const QString& sortBy);
    bool descending() const { return m_descending; }
    void setDescending(bool descending);
    PlayedFilter playedFilter() const { return m_played; }
    void setPlayedFilter(PlayedFilter filter);
    bool favoritesOnly() const { return m_favoritesOnly; }
    void setFavoritesOnly(bool only);
    QStringList genres() const { return m_genres; }
    void setGenres(const QStringList& genres);
    QVariantList years() const;
    void setYears(const QVariantList& years);
    // A movie library lists its collections instead of its movies, as the
    // Collections tab of Jellyfin Web does.
    bool showCollections() const { return m_showCollections; }
    void setShowCollections(bool show);
    bool canShowCollections() const;
    // The library's or collection's name, from the server.
    QString name() const { return m_name; }

    // Sort, filters and the collections switch, to keep per library.
    QJsonObject filters() const;
    // Applies what filters() gave; unknown or missing keys keep their value.
    void applyFilters(const QJsonObject& filters);

    QStringList availableGenres() const { return m_filters.genres; }
    QVariantList availableYears() const;

    int count() const { return m_total; }
    bool loading() const { return !m_inFlight.isEmpty(); }
    QString errorText() const { return m_errorText; }
    // The item at `row` if its page has loaded.
    const api::Item* item(int row) const;
    int loadedPages() const { return static_cast<int>(m_pages.size()); }

    // Item types listed for a library type: Movie for movies, Series for
    // shows, and so on.
    static QStringList itemTypesFor(const QString& collectionType);
    static inline const QString CollectionItems = QStringLiteral("jfd-collection");

    void follow(LibraryUpdates& updates);

    // Starts the listing over (also done by every property change).
    Q_INVOKABLE void reload();
    // Loads the page holding `row` (QML calls this for rows coming into view;
    // data() does it too).
    Q_INVOKABLE void ensureLoaded(int row);

  Q_SIGNALS:
    void queryChanged();
    void filtersChanged();
    void nameChanged();
    void countChanged();
    void loadingChanged();
    void errorTextChanged();

  private:
    void scheduleReload();
    void loadPage(int page);
    void setError(const QString& text);
    void setTotal(int total);
    QStringList itemTypes() const;

    api::JellyfinClient& m_client;
    Options m_options;
    QString m_libraryId;
    QString m_collectionType;
    QString m_sortBy = QStringLiteral("SortName");
    bool m_descending = false;
    PlayedFilter m_played = PlayedFilter::All;
    bool m_favoritesOnly = false;
    QStringList m_genres;
    QList<int> m_years;
    bool m_showCollections = false;
    QString m_name;

    quint64 m_generation = 0;
    bool m_reloadScheduled = false;
    int m_total = 0;
    QHash<int, QList<ItemListModel::Entry>> m_pages;
    QSet<int> m_inFlight;
    api::ItemFilters m_filters;
    QString m_errorText;
  };

} // namespace jfd::library
