#include <jfd/ui/MusicListModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/library/CardImages.h>
#include <jfd/music/MusicPlayer.h>

#include <QCoreApplication>
#include <QTimer>

#include <algorithm>

namespace jfd::ui {

  namespace {
    // Lists the server answers in one piece get one page this big.
    constexpr int WholePage = 10'000;
    constexpr int RecentlyPlayedCount = 50;

    api::MusicSort sortFor(const QString& name) {
      if (name == QLatin1String("added")) return api::MusicSort::DateAdded;
      if (name == QLatin1String("year")) return api::MusicSort::Year;
      if (name == QLatin1String("random")) return api::MusicSort::Random;
      if (name == QLatin1String("plays")) return api::MusicSort::PlayCount;
      if (name == QLatin1String("played")) return api::MusicSort::LastPlayed;
      return api::MusicSort::Name;
    }

    QString text(const char* source, int count) {
      return QCoreApplication::translate("MusicList", source, nullptr, count);
    }
  } // namespace

  MusicListModel::MusicListModel(api::JellyfinClient& client, music::MusicPlayer& player, Options options,
                                 QObject* parent)
      : QAbstractListModel(parent), m_client(client), m_player(player), m_api(client), m_options(options) {
    // The first listing, with whatever the page sets up before it runs.
    scheduleReload();
  }

  bool MusicListModel::isTrackKind() const {
    return m_kind == QLatin1String("songs") || m_kind == QLatin1String("albumTracks") ||
           m_kind == QLatin1String("playlistTracks") || m_kind == QLatin1String("recentlyPlayed");
  }

  bool MusicListModel::isWhole() const {
    return m_kind == QLatin1String("albumTracks") || m_kind == QLatin1String("playlistTracks") ||
           m_kind == QLatin1String("recentlyPlayed");
  }

  int MusicListModel::pageSize() const { return isWhole() ? WholePage : m_options.pageSize; }

  int MusicListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_total; }

  const api::Item* MusicListModel::item(int row) const {
    if (row < 0 || row >= m_total) return nullptr;
    const auto page = m_pages.constFind(row / pageSize());
    if (page == m_pages.constEnd()) return nullptr;
    const int at = row % pageSize();
    return at < page->size() ? &(*page)[at].item : nullptr;
  }

  QUrl MusicListModel::imageFor(const api::Item& item) const {
    const QSize pixels = library::cardImageSize(library::CardShape::Square, m_options.imageWidth,
                                                m_options.devicePixelRatio);
    api::ImageRequest request;
    request.type = QStringLiteral("Primary");
    request.fillWidth = pixels.width();
    request.fillHeight = pixels.height();
    // A track shows its album's cover.
    if (item.type == QLatin1String("Audio") && !item.albumId.isEmpty() && !item.albumPrimaryImageTag.isEmpty()) {
      request.itemId = item.albumId;
      request.tag = item.albumPrimaryImageTag;
    } else {
      request.itemId = item.id;
      request.tag = item.primaryImageTag;
    }
    if (request.tag.isEmpty()) return {};
    return m_client.imageUrl(request);
  }

  QString MusicListModel::subtitleFor(const api::Item& item) const {
    if (item.type == QLatin1String("MusicAlbum")) {
      QString line = item.albumArtist;
      if (item.productionYear) {
        if (!line.isEmpty()) line += QStringLiteral(" · ");
        line += QString::number(*item.productionYear);
      }
      return line;
    }
    if (item.type == QLatin1String("MusicArtist"))
      return item.albumCount ? text("%n album(s)", *item.albumCount) : QString();
    if (item.type == QLatin1String("Playlist"))
      return item.childCount ? text("%n track(s)", *item.childCount) : QString();
    if (item.type == QLatin1String("Audio"))
      return item.artists.isEmpty() ? item.albumArtist : item.artists.join(QStringLiteral(", "));
    return {};
  }

  QVariant MusicListModel::data(const QModelIndex& index, int role) const {
    const int row = index.row();
    if (index.parent().isValid() || row < 0 || row >= m_total) return {};
    const int pageIndex = row / pageSize();
    const auto page = m_pages.constFind(pageIndex);
    const int at = row % pageSize();
    if (page == m_pages.constEnd() || at >= page->size()) {
      // Load it from the event loop: models must not change inside data().
      if (!m_inFlight.contains(pageIndex)) {
        auto* self = const_cast<MusicListModel*>(this);
        QTimer::singleShot(0, self, [self, row]() { self->ensureLoaded(row); });
      }
      return role == LoadedRole ? QVariant(false) : QVariant();
    }
    const Entry& entry = (*page)[at];
    const api::Item& item = entry.item;
    switch (role) {
    case ItemIdRole: return item.id;
    case NameRole: return item.name;
    case SubtitleRole: return subtitleFor(item);
    case ImageUrlRole: return entry.image;
    case TypeRole: return item.type;
    case DurationRole: return static_cast<double>(item.runTimeTicks.value_or(0)) / 1e7;
    case IndexNumberRole: return item.indexNumber ? QVariant(*item.indexNumber) : QVariant(0);
    case DiscNumberRole: return item.parentIndexNumber ? QVariant(*item.parentIndexNumber) : QVariant(0);
    case YearRole: return item.productionYear ? QVariant(*item.productionYear) : QVariant(0);
    case AlbumIdRole: return item.albumId;
    case AlbumNameRole: return item.album;
    case ArtistIdRole:
      if (!item.artistItems.isEmpty()) return item.artistItems.front().id;
      return item.albumArtists.isEmpty() ? QString() : item.albumArtists.front().id;
    case ArtistNameRole:
      return item.artists.isEmpty() ? item.albumArtist : item.artists.join(QStringLiteral(", "));
    case PlaylistItemIdRole: return item.playlistItemId;
    case FavoriteRole: return item.userData.isFavorite;
    case LoadedRole: return true;
    }
    return {};
  }

  QHash<int, QByteArray> MusicListModel::roleNames() const {
    return {{ItemIdRole, "itemId"},         {NameRole, "name"},
            {SubtitleRole, "subtitle"},     {ImageUrlRole, "imageUrl"},
            {TypeRole, "type"},             {DurationRole, "duration"},
            {IndexNumberRole, "indexNumber"}, {DiscNumberRole, "discNumber"},
            {YearRole, "year"},             {AlbumIdRole, "albumId"},
            {AlbumNameRole, "albumName"},   {ArtistIdRole, "artistId"},
            {ArtistNameRole, "artistName"}, {PlaylistItemIdRole, "playlistItemId"},
            {FavoriteRole, "favorite"},     {LoadedRole, "loaded"}};
  }

  double MusicListModel::totalSeconds() const {
    double seconds = 0;
    for (const QList<Entry>& page : m_pages)
      for (const Entry& entry : page)
        seconds += static_cast<double>(entry.item.runTimeTicks.value_or(0)) / 1e7;
    return seconds;
  }

  // --- The query -----------------------------------------------------------------

#define JFD_SET_QUERY(member, value) \
  if (member == value) return;       \
  member = value;                    \
  scheduleReload();

  void MusicListModel::setKind(const QString& kind) { JFD_SET_QUERY(m_kind, kind) }
  void MusicListModel::setParentId(const QString& id) { JFD_SET_QUERY(m_parentId, id) }
  void MusicListModel::setArtistId(const QString& id) { JFD_SET_QUERY(m_artistId, id) }
  void MusicListModel::setGenre(const QString& genre) { JFD_SET_QUERY(m_genre, genre) }
  void MusicListModel::setSortBy(const QString& sortBy) { JFD_SET_QUERY(m_sortBy, sortBy) }
  void MusicListModel::setDescending(bool descending) { JFD_SET_QUERY(m_descending, descending) }
  void MusicListModel::setSearchTerm(const QString& term) { JFD_SET_QUERY(m_searchTerm, term) }
  void MusicListModel::setFavoritesOnly(bool only) { JFD_SET_QUERY(m_favoritesOnly, only) }

#undef JFD_SET_QUERY

  void MusicListModel::scheduleReload() {
    Q_EMIT queryChanged();
    if (m_reloadScheduled) return;
    m_reloadScheduled = true;
    // Several properties change together when a page is set up: one listing.
    QTimer::singleShot(0, this, [this] {
      m_reloadScheduled = false;
      reload();
    });
  }

  api::MusicQuery MusicListModel::query(int page) const {
    api::MusicQuery q;
    q.parentId = m_parentId;
    q.searchTerm = m_searchTerm;
    q.sort = sortFor(m_sortBy);
    q.descending = m_descending;
    q.startIndex = page * pageSize();
    q.limit = pageSize();
    if (!m_genre.isEmpty()) q.genres = {m_genre};
    if (m_favoritesOnly) q.isFavorite = true;
    if (!m_artistId.isEmpty()) {
      if (m_kind == QLatin1String("albums")) q.albumArtistIds = {m_artistId};
      else q.artistIds = {m_artistId};
    }
    return q;
  }

  void MusicListModel::reload() {
    ++m_generation;
    m_inFlight.clear();
    beginResetModel();
    m_pages.clear();
    m_total = 0;
    endResetModel();
    Q_EMIT countChanged();
    setError({});
    Q_EMIT loadingChanged();
    // Lists with a parent wait for it; the others list the whole library.
    const bool needsParent = m_kind == QLatin1String("albumTracks") || m_kind == QLatin1String("playlistTracks");
    if (needsParent && m_parentId.isEmpty()) return;
    loadPage(0);
  }

  void MusicListModel::ensureLoaded(int row) {
    if (row < 0 || row >= m_total) return;
    const int page = row / pageSize();
    if (!m_pages.contains(page) && !m_inFlight.contains(page)) loadPage(page);
  }

  void MusicListModel::loadPage(int page) {
    const quint64 generation = m_generation;
    const bool wasLoading = loading();
    m_inFlight.insert(page);
    if (!wasLoading) Q_EMIT loadingChanged();
    const auto done = [this, page, generation](api::Result<api::ItemPage> result) {
      if (generation != m_generation) return;
      m_inFlight.remove(page);
      if (!result) {
        setError(result.error().message);
        Q_EMIT loadingChanged();
        return;
      }
      QList<Entry> entries;
      entries.reserve(result->items.size());
      for (const api::Item& item : result->items)
        entries.append({item, imageFor(item)});
      const int total = isWhole() ? static_cast<int>(entries.size()) : result->totalRecordCount;
      // The page first, so the rows that appear already have their data.
      m_pages.insert(page, std::move(entries));
      setTotal(total);
      const int first = page * pageSize();
      const int last = std::min(first + static_cast<int>(m_pages[page].size()), m_total) - 1;
      if (last >= first) Q_EMIT dataChanged(index(first), index(last));
      Q_EMIT countChanged();
      Q_EMIT loadingChanged();
    };
    const api::MusicQuery q = query(page);
    if (m_kind == QLatin1String("albums")) m_api.albums(q, this, done);
    else if (m_kind == QLatin1String("artists")) m_api.artists(q, false, this, done);
    else if (m_kind == QLatin1String("songs")) m_api.songs(q, this, done);
    else if (m_kind == QLatin1String("playlists")) m_api.playlists(q, this, done);
    else if (m_kind == QLatin1String("genres")) m_api.genres(q, this, done);
    else if (m_kind == QLatin1String("albumTracks")) m_api.albumTracks(m_parentId, this, done);
    else if (m_kind == QLatin1String("playlistTracks")) m_api.playlistTracks(m_parentId, this, done);
    else if (m_kind == QLatin1String("recentlyPlayed")) m_api.recentlyPlayed(m_parentId, RecentlyPlayedCount, this, done);
    else {
      m_inFlight.remove(page);
      setError(QStringLiteral("Unknown music list: ") + m_kind);
      Q_EMIT loadingChanged();
    }
  }

  void MusicListModel::setTotal(int total) {
    if (total == m_total) return;
    if (total > m_total) {
      beginInsertRows({}, m_total, total - 1);
      m_total = total;
      endInsertRows();
    } else {
      beginRemoveRows({}, total, m_total - 1);
      m_total = total;
      endRemoveRows();
    }
    Q_EMIT countChanged();
  }

  void MusicListModel::setError(const QString& text) {
    if (text == m_errorText) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

  // --- Actions -------------------------------------------------------------------

  QString MusicListModel::routeFor(int row) const {
    const api::Item* it = item(row);
    if (!it) return {};
    if (it->type == QLatin1String("MusicAlbum")) return QStringLiteral("album/") + it->id;
    if (it->type == QLatin1String("MusicArtist")) return QStringLiteral("artist/") + it->id;
    if (it->type == QLatin1String("Playlist")) return QStringLiteral("playlist/") + it->id;
    if (it->type == QLatin1String("MusicGenre")) return QStringLiteral("genre/") + it->name;
    return {};
  }

  void MusicListModel::play(int row, bool shuffle) {
    const api::Item* it = item(row);
    if (!it) return;
    if (it->type == QLatin1String("MusicAlbum")) {
      m_player.playAlbum(it->id, 0, shuffle);
    } else if (it->type == QLatin1String("MusicArtist")) {
      m_player.playArtist(it->id, shuffle);
    } else if (it->type == QLatin1String("Playlist")) {
      m_player.playPlaylist(it->id, 0, shuffle);
    } else if (it->type == QLatin1String("Audio")) {
      // The tracks of its page, from this one on, are the queue.
      const auto page = m_pages.constFind(row / pageSize());
      if (page == m_pages.constEnd()) return;
      QList<music::Track> tracks;
      for (const Entry& entry : *page)
        if (entry.item.type == QLatin1String("Audio")) tracks.append(music::Track::fromItem(entry.item));
      m_player.setShuffle(shuffle);
      m_player.play(std::move(tracks), row % pageSize());
    }
  }

  void MusicListModel::playAll(bool shuffle) {
    if (!isTrackKind()) return;
    const auto page = m_pages.constFind(0);
    if (page == m_pages.constEnd() || page->isEmpty()) return;
    QList<music::Track> tracks;
    for (const Entry& entry : *page)
      if (entry.item.type == QLatin1String("Audio")) tracks.append(music::Track::fromItem(entry.item));
    m_player.setShuffle(shuffle);
    m_player.play(std::move(tracks), 0);
  }

  void MusicListModel::enqueue(int row, bool next) {
    const api::Item* it = item(row);
    if (!it) return;
    if (it->type == QLatin1String("MusicAlbum")) {
      m_player.queueAlbum(it->id, next);
    } else if (it->type == QLatin1String("Audio")) {
      QList<music::Track> tracks{music::Track::fromItem(*it)};
      if (next) m_player.playNext(std::move(tracks));
      else m_player.append(std::move(tracks));
    }
  }

} // namespace jfd::ui
