#pragma once

#include <jfd/api/JellyfinClient.h>

#include <QString>
#include <QStringList>

// The music library: artists, albums, tracks, genres and playlists. A thin
// layer over JellyfinClient; every call answers items in the same Item type,
// with the music fields (album, artists, counts) filled in.
namespace jfd::api {

  enum class MusicSort { Name, DateAdded, Year, Random, PlayCount, LastPlayed };

  struct MusicQuery {
    // The music library's id; empty for all of the user's libraries.
    QString parentId;
    QString searchTerm;
    MusicSort sort = MusicSort::Name;
    bool descending = false;
    int startIndex = 0;
    int limit = 100;
    QStringList genres;
    std::optional<bool> isFavorite;
    // Albums and tracks of these album artists (or artists).
    QStringList albumArtistIds;
    QStringList artistIds;
  };

  class MusicApi {
  public:
    explicit MusicApi(JellyfinClient& client) : m_client(client) {}

    // GET Items, MusicAlbum.
    void albums(const MusicQuery& query, QObject* context, Callback<ItemPage> done);
    // GET Items, Audio: loose tracks, or an artist's.
    void songs(const MusicQuery& query, QObject* context, Callback<ItemPage> done);
    // GET Artists/AlbumArtists, or Artists with `all`.
    void artists(const MusicQuery& query, bool all, QObject* context, Callback<ItemPage> done);
    // GET MusicGenres.
    void genres(const MusicQuery& query, QObject* context, Callback<ItemPage> done);
    // GET Items, Playlist, music ones only.
    void playlists(const MusicQuery& query, QObject* context, Callback<ItemPage> done);
    // An album's tracks in disc and track order.
    void albumTracks(const QString& albumId, QObject* context, Callback<ItemPage> done);
    // GET Playlists/{id}/Items: tracks with their playlistItemId.
    void playlistTracks(const QString& playlistId, QObject* context, Callback<ItemPage> done);
    // GET Items/Latest: recently added albums.
    void latestAlbums(const QString& parentId, int limit, QObject* context, Callback<ItemPage> done);
    // GET Items with SortBy DatePlayed: recently played tracks.
    void recentlyPlayed(const QString& parentId, int limit, QObject* context, Callback<ItemPage> done);
    // GET Albums/{id}/Similar.
    void similarAlbums(const QString& albumId, int limit, QObject* context, Callback<ItemPage> done);

    // POST Playlists: a new music playlist holding `trackIds`. Answers its id.
    void createPlaylist(const QString& name, const QStringList& trackIds, QObject* context,
                        Callback<QString> done);
    // POST Playlists/{id}/Items.
    void addToPlaylist(const QString& playlistId, const QStringList& trackIds, QObject* context,
                       Callback<Empty> done);
    // DELETE Playlists/{id}/Items, by playlistItemId.
    void removeFromPlaylist(const QString& playlistId, const QStringList& entryIds, QObject* context,
                            Callback<Empty> done);

    // The ItemSortBy names a sort stands for.
    static QString sortName(MusicSort sort);

  private:
    QUrlQuery listQuery(const MusicQuery& query) const;

    JellyfinClient& m_client;
  };

} // namespace jfd::api
