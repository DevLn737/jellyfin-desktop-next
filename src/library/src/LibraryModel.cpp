#include <jfd/library/LibraryModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/LibraryUpdates.h>

#include <QJsonArray>
#include <QTimer>

#include <algorithm>
#include <iterator>

namespace jfd::library {

  LibraryModel::LibraryModel(api::JellyfinClient& client, Options options, QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_options(options) {
    if (m_options.pageSize < 1) m_options.pageSize = 1;
  }

  QStringList LibraryModel::itemTypesFor(const QString& collectionType) {
    if (collectionType == QLatin1String("movies")) return {QStringLiteral("Movie")};
    if (collectionType == QLatin1String("tvshows")) return {QStringLiteral("Series")};
    if (collectionType == QLatin1String("homevideos")) return {QStringLiteral("Video")};
    if (collectionType == QLatin1String("musicvideos")) return {QStringLiteral("MusicVideo")};
    if (collectionType == QLatin1String("boxsets")) return {QStringLiteral("BoxSet")};
    // Whatever the collection holds.
    if (collectionType == CollectionItems) return {};
    return {QStringLiteral("Movie"), QStringLiteral("Series"), QStringLiteral("Video")};
  }

  int LibraryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_total;
  }

  const api::Item* LibraryModel::item(int row) const {
    if (row < 0 || row >= m_total) return nullptr;
    const auto page = m_pages.constFind(row / m_options.pageSize);
    if (page == m_pages.constEnd()) return nullptr;
    const int at = row % m_options.pageSize;
    return at < page->size() ? &(*page)[at].item : nullptr;
  }

  QVariant LibraryModel::data(const QModelIndex& index, int role) const {
    const int row = index.row();
    if (index.parent().isValid() || row < 0 || row >= m_total) return {};
    const int pageIndex = row / m_options.pageSize;
    const auto page = m_pages.constFind(pageIndex);
    const int at = row % m_options.pageSize;
    if (page == m_pages.constEnd() || at >= page->size()) {
      // Load it from the event loop: models must not change inside data().
      if (!m_inFlight.contains(pageIndex)) {
        auto* self = const_cast<LibraryModel*>(this);
        QTimer::singleShot(0, self, [self, row]() { self->ensureLoaded(row); });
      }
      return role == LoadedRole ? QVariant(false) : QVariant();
    }
    if (role == LoadedRole) return true;
    return ItemListModel::entryData((*page)[at], role);
  }

  QHash<int, QByteArray> LibraryModel::roleNames() const {
    QHash<int, QByteArray> names = ItemListModel::itemRoleNames();
    names.insert(LoadedRole, "loaded");
    return names;
  }

  void LibraryModel::setLibraryId(const QString& id) {
    if (m_libraryId == id) return;
    m_libraryId = id;
    if (!m_name.isEmpty()) {
      m_name.clear();
      Q_EMIT nameChanged();
    }
    scheduleReload();
    if (id.isEmpty()) return;
    m_client.item(id, this, [this, id](api::Result<api::Item> result) {
      if (id != m_libraryId || !result || result->name == m_name) return;
      m_name = result->name;
      Q_EMIT nameChanged();
    });
  }

  void LibraryModel::setCollectionType(const QString& type) {
    if (m_collectionType == type) return;
    m_collectionType = type;
    scheduleReload();
  }

  void LibraryModel::setSortBy(const QString& sortBy) {
    if (m_sortBy == sortBy) return;
    m_sortBy = sortBy;
    scheduleReload();
  }

  void LibraryModel::setDescending(bool descending) {
    if (m_descending == descending) return;
    m_descending = descending;
    scheduleReload();
  }

  void LibraryModel::setPlayedFilter(PlayedFilter filter) {
    if (m_played == filter) return;
    m_played = filter;
    scheduleReload();
  }

  void LibraryModel::setFavoritesOnly(bool only) {
    if (m_favoritesOnly == only) return;
    m_favoritesOnly = only;
    scheduleReload();
  }

  void LibraryModel::setGenres(const QStringList& genres) {
    if (m_genres == genres) return;
    m_genres = genres;
    scheduleReload();
  }

  QVariantList LibraryModel::years() const {
    QVariantList out;
    for (int year : m_years)
      out.append(year);
    return out;
  }

  void LibraryModel::setYears(const QVariantList& years) {
    QList<int> parsed;
    for (const QVariant& year : years)
      parsed.append(year.toInt());
    if (m_years == parsed) return;
    m_years = parsed;
    scheduleReload();
  }

  void LibraryModel::setShowCollections(bool show) {
    if (m_showCollections == show) return;
    m_showCollections = show;
    scheduleReload();
  }

  bool LibraryModel::canShowCollections() const {
    return m_collectionType == QLatin1String("movies");
  }

  QStringList LibraryModel::itemTypes() const {
    if (m_showCollections && canShowCollections()) return {QStringLiteral("BoxSet")};
    return itemTypesFor(m_collectionType);
  }

  QJsonObject LibraryModel::filters() const {
    QJsonObject out;
    out.insert(QStringLiteral("sortBy"), m_sortBy);
    out.insert(QStringLiteral("descending"), m_descending);
    out.insert(QStringLiteral("played"), m_played == PlayedFilter::Played ? QStringLiteral("played")
                                         : m_played == PlayedFilter::Unplayed
                                             ? QStringLiteral("unplayed")
                                             : QStringLiteral("all"));
    out.insert(QStringLiteral("favorites"), m_favoritesOnly);
    out.insert(QStringLiteral("genres"), QJsonArray::fromStringList(m_genres));
    QJsonArray years;
    for (int year : m_years)
      years.append(year);
    out.insert(QStringLiteral("years"), years);
    out.insert(QStringLiteral("collections"), m_showCollections);
    return out;
  }

  void LibraryModel::applyFilters(const QJsonObject& filters) {
    if (const QJsonValue sortBy = filters.value(QStringLiteral("sortBy")); sortBy.isString())
      setSortBy(sortBy.toString());
    if (const QJsonValue descending = filters.value(QStringLiteral("descending"));
        descending.isBool())
      setDescending(descending.toBool());
    const QString played = filters.value(QStringLiteral("played")).toString();
    if (played == QLatin1String("played")) setPlayedFilter(PlayedFilter::Played);
    if (played == QLatin1String("unplayed")) setPlayedFilter(PlayedFilter::Unplayed);
    if (played == QLatin1String("all")) setPlayedFilter(PlayedFilter::All);
    if (const QJsonValue favorites = filters.value(QStringLiteral("favorites")); favorites.isBool())
      setFavoritesOnly(favorites.toBool());
    if (const QJsonValue genres = filters.value(QStringLiteral("genres")); genres.isArray()) {
      QStringList list;
      for (const QJsonValue& genre : genres.toArray())
        if (genre.isString()) list.append(genre.toString());
      setGenres(list);
    }
    if (const QJsonValue years = filters.value(QStringLiteral("years")); years.isArray())
      setYears(years.toArray().toVariantList());
    if (const QJsonValue collections = filters.value(QStringLiteral("collections"));
        collections.isBool())
      setShowCollections(collections.toBool());
  }

  QVariantList LibraryModel::availableYears() const {
    QVariantList out;
    for (int year : m_filters.years)
      out.append(year);
    return out;
  }

  void LibraryModel::follow(LibraryUpdates& updates) {
    connect(&updates, &LibraryUpdates::userDataChanged, this,
            [this](const api::UserItemData& data) {
              for (auto page = m_pages.begin(); page != m_pages.end(); ++page) {
                for (int at = 0; at < page->size(); ++at) {
                  api::Item& item = (*page)[at].item;
                  if (item.id != data.itemId) continue;
                  item.userData = mergeUserData(item.userData, data);
                  const QModelIndex i = index(page.key() * m_options.pageSize + at);
                  Q_EMIT dataChanged(i, i);
                }
              }
            });
    connect(&updates, &LibraryUpdates::libraryChanged, this,
            [this](const QStringList& added, const QStringList&, const QStringList& removed) {
              // Additions and removals shift every later row: start over.
              if (!added.isEmpty() || !removed.isEmpty()) scheduleReload();
            });
    connect(&updates, &LibraryUpdates::resynchronize, this, &LibraryModel::scheduleReload);
  }

  void LibraryModel::scheduleReload() {
    Q_EMIT queryChanged();
    if (m_reloadScheduled) return;
    // Several properties set in a row (QML bindings at creation) cost one listing.
    m_reloadScheduled = true;
    QTimer::singleShot(0, this, [this]() {
      if (m_reloadScheduled) reload();
    });
  }

  void LibraryModel::reload() {
    m_reloadScheduled = false;
    ++m_generation;
    const bool wasLoading = loading();
    beginResetModel();
    m_pages.clear();
    m_inFlight.clear();
    m_total = 0;
    endResetModel();
    Q_EMIT countChanged();
    if (wasLoading) Q_EMIT loadingChanged();
    setError({});
    if (m_libraryId.isEmpty()) return;

    loadPage(0);
    const quint64 generation = m_generation;
    m_client.itemFilters(m_libraryId, itemTypes(), this,
                         [this, generation](api::Result<api::ItemFilters> result) {
                           if (generation != m_generation || !result) return;
                           m_filters = result.value();
                           Q_EMIT filtersChanged();
                         });
  }

  void LibraryModel::ensureLoaded(int row) {
    if (row < 0 || (m_total > 0 && row >= m_total)) return;
    const int page = row / m_options.pageSize;
    if (!m_pages.contains(page)) loadPage(page);
  }

  void LibraryModel::loadPage(int page) {
    if (m_inFlight.contains(page) || m_libraryId.isEmpty()) return;
    const bool wasLoading = loading();
    m_inFlight.insert(page);
    if (!wasLoading) Q_EMIT loadingChanged();

    api::ItemQuery query;
    query.parentId = m_libraryId;
    query.includeItemTypes = itemTypes();
    // A collection's items are its own children; a library's are anywhere
    // below it.
    query.recursive = m_collectionType != CollectionItems;
    // A stable second key, so pages never overlap or skip items with equal
    // sort values.
    query.sortBy =
        m_sortBy == QLatin1String("SortName") ? m_sortBy : m_sortBy + QStringLiteral(",SortName");
    query.descending = m_descending;
    query.genres = m_genres;
    query.years = m_years;
    if (m_played != PlayedFilter::All) query.isPlayed = m_played == PlayedFilter::Played;
    if (m_favoritesOnly) query.isFavorite = true;
    query.startIndex = page * m_options.pageSize;
    query.limit = m_options.pageSize;

    const quint64 generation = m_generation;
    m_client.items(query, this, [this, generation, page](api::Result<api::ItemPage> result) {
      if (generation != m_generation) return;
      m_inFlight.remove(page);
      if (!result) {
        setError(result.error().message);
      } else {
        const QSize pixels =
            cardImageSize(CardShape::Poster, m_options.cardWidth, m_options.devicePixelRatio);
        QList<ItemListModel::Entry> entries;
        entries.reserve(result->items.size());
        for (const api::Item& item : result->items)
          entries.append({item, cardImageUrl(m_client, item, CardShape::Poster, pixels)});
        m_pages.insert(page, std::move(entries));
        setTotal(result->totalRecordCount);
        const int first = page * m_options.pageSize;
        const int last = std::min(m_total, first + m_options.pageSize) - 1;
        if (last >= first) Q_EMIT dataChanged(index(first), index(last));
      }
      if (!loading()) Q_EMIT loadingChanged();
    });
  }

  void LibraryModel::setTotal(int total) {
    if (total == m_total) return;
    if (total > m_total) {
      beginInsertRows({}, m_total, total - 1);
      m_total = total;
      endInsertRows();
    } else {
      // The library shrank since the first page: drop pages past the end.
      beginRemoveRows({}, total, m_total - 1);
      m_total = total;
      for (auto it = m_pages.begin(); it != m_pages.end();)
        it = it.key() * m_options.pageSize >= total ? m_pages.erase(it) : std::next(it);
      endRemoveRows();
    }
    Q_EMIT countChanged();
  }

  void LibraryModel::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

} // namespace jfd::library
