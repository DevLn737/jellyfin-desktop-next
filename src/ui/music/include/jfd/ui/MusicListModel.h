#pragma once

#include <jfd/api/Models.h>
#include <jfd/api/MusicApi.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace jfd::api {
  class JellyfinClient;
}
namespace jfd::music {
  class MusicPlayer;
}

namespace jfd::ui {

  // One music list for QML: albums, artists, songs, playlists or genres of a
  // library, an artist's albums, an album's or playlist's tracks. Every row
  // has its place from the start (the count comes with the first page) and
  // pages load when their rows are first asked for, so a library of tens
  // of thousands of albums scrolls at once and loads only what is shown.
  // A change of kind, filter or sort starts over; pages of the earlier
  // listing are dropped when they arrive.
  class MusicListModel : public QAbstractListModel {
    Q_OBJECT
    // albums, artists, songs, playlists, genres, albumTracks,
    // playlistTracks, recentlyPlayed.
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY queryChanged)
    // The album or playlist of its tracks; the music library otherwise
    // (empty: every library).
    Q_PROPERTY(QString parentId READ parentId WRITE setParentId NOTIFY queryChanged)
    // Albums and songs of one (album) artist.
    Q_PROPERTY(QString artistId READ artistId WRITE setArtistId NOTIFY queryChanged)
    Q_PROPERTY(QString genre READ genre WRITE setGenre NOTIFY queryChanged)
    // name, added, year, random, plays, played.
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY queryChanged)
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY queryChanged)
    Q_PROPERTY(QString searchTerm READ searchTerm WRITE setSearchTerm NOTIFY queryChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY queryChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    // The loaded tracks' length in seconds (an album's or playlist's).
    Q_PROPERTY(double totalSeconds READ totalSeconds NOTIFY countChanged)

  public:
    enum Role {
      ItemIdRole = Qt::UserRole + 1,
      NameRole,
      // The line under the name: the artist and year of an album, the
      // album count of an artist.
      SubtitleRole,
      ImageUrlRole,
      TypeRole,
      DurationRole,
      IndexNumberRole,
      DiscNumberRole,
      YearRole,
      AlbumIdRole,
      AlbumNameRole,
      ArtistIdRole,
      ArtistNameRole,
      PlaylistItemIdRole,
      FavoriteRole,
      LoadedRole,
    };
    Q_ENUM(Role)

    struct Options {
      int pageSize = 100;
      // The cards' and rows' image width in logical pixels.
      int imageWidth = 200;
      qreal devicePixelRatio = 1.0;
    };

    MusicListModel(api::JellyfinClient& client, music::MusicPlayer& player, Options options,
                   QObject* parent = nullptr);
    MusicListModel(api::JellyfinClient& client, music::MusicPlayer& player, QObject* parent = nullptr)
        : MusicListModel(client, player, Options{}, parent) {}

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString kind() const { return m_kind; }
    void setKind(const QString& kind);
    QString parentId() const { return m_parentId; }
    void setParentId(const QString& id);
    QString artistId() const { return m_artistId; }
    void setArtistId(const QString& id);
    QString genre() const { return m_genre; }
    void setGenre(const QString& genre);
    QString sortBy() const { return m_sortBy; }
    void setSortBy(const QString& sortBy);
    bool descending() const { return m_descending; }
    void setDescending(bool descending);
    QString searchTerm() const { return m_searchTerm; }
    void setSearchTerm(const QString& term);
    bool favoritesOnly() const { return m_favoritesOnly; }
    void setFavoritesOnly(bool only);

    int count() const { return m_total; }
    bool loading() const { return !m_inFlight.isEmpty(); }
    QString errorText() const { return m_errorText; }
    double totalSeconds() const;
    int loadedPages() const { return static_cast<int>(m_pages.size()); }
    const api::Item* item(int row) const;

    // QML calls this for rows coming into view; data() does it too.
    Q_INVOKABLE void ensureLoaded(int row);
    Q_INVOKABLE void reload();

    // The page for a row: "album/<id>", "artist/<id>", "playlist/<id>",
    // "genre/<name>"; empty for tracks.
    Q_INVOKABLE QString routeFor(int row) const;
    // Plays the row: an album, artist or playlist as a whole, a track from
    // its list (the loaded tracks of its page follow it).
    Q_INVOKABLE void play(int row, bool shuffle = false);
    // Plays every track of an album or playlist list.
    Q_INVOKABLE void playAll(bool shuffle);
    // Adds the row to the end of the queue, or right after the current track.
    Q_INVOKABLE void enqueue(int row, bool next);

  Q_SIGNALS:
    void queryChanged();
    void countChanged();
    void loadingChanged();
    void errorTextChanged();

  private:
    bool isTrackKind() const;
    // Lists the server answers whole, without paging.
    bool isWhole() const;
    int pageSize() const;
    void scheduleReload();
    void loadPage(int page);
    void setError(const QString& text);
    void setTotal(int total);
    api::MusicQuery query(int page) const;
    QUrl imageFor(const api::Item& item) const;
    QString subtitleFor(const api::Item& item) const;

    api::JellyfinClient& m_client;
    music::MusicPlayer& m_player;
    api::MusicApi m_api;
    Options m_options;
    QString m_kind = QStringLiteral("albums");
    QString m_parentId;
    QString m_artistId;
    QString m_genre;
    QString m_sortBy = QStringLiteral("name");
    bool m_descending = false;
    QString m_searchTerm;
    bool m_favoritesOnly = false;

    quint64 m_generation = 0;
    bool m_reloadScheduled = false;
    int m_total = 0;
    struct Entry {
      api::Item item;
      QUrl image;
    };
    QHash<int, QList<Entry>> m_pages;
    QSet<int> m_inFlight;
    QString m_errorText;
  };

} // namespace jfd::ui
