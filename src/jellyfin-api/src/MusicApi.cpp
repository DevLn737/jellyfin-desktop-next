#include <jfd/api/MusicApi.h>

#include <QJsonArray>

namespace jfd::api {

  namespace {
    // The fields music lists need besides what the server always sends.
    const QString MusicFields =
        QStringLiteral("PrimaryImageAspectRatio,ChildCount,ItemCounts,Genres,ProductionYear,DateCreated");

    QString joined(const QStringList& values, QChar separator = QLatin1Char(',')) {
      return values.join(separator);
    }

    template <class T, class Parse> Callback<QJsonObject> parsed(Callback<T> done, Parse parse) {
      return [done = std::move(done), parse](Result<QJsonObject> result) {
        if (!result) return done(result.error());
        auto value = parse(result.value());
        if (!value) return done(value.error());
        done(std::move(value).value());
      };
    }
  } // namespace

  QString MusicApi::sortName(MusicSort sort) {
    switch (sort) {
    case MusicSort::Name: return QStringLiteral("SortName");
    case MusicSort::DateAdded: return QStringLiteral("DateCreated");
    case MusicSort::Year: return QStringLiteral("ProductionYear,SortName");
    case MusicSort::Random: return QStringLiteral("Random");
    case MusicSort::PlayCount: return QStringLiteral("PlayCount");
    case MusicSort::LastPlayed: return QStringLiteral("DatePlayed");
    }
    return QStringLiteral("SortName");
  }

  QUrlQuery MusicApi::listQuery(const MusicQuery& q) const {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    if (!q.parentId.isEmpty()) query.addQueryItem(QStringLiteral("parentId"), q.parentId);
    if (!q.searchTerm.isEmpty()) query.addQueryItem(QStringLiteral("searchTerm"), q.searchTerm);
    query.addQueryItem(QStringLiteral("sortBy"), sortName(q.sort));
    query.addQueryItem(QStringLiteral("sortOrder"),
                       q.descending ? QStringLiteral("Descending") : QStringLiteral("Ascending"));
    if (!q.genres.isEmpty()) query.addQueryItem(QStringLiteral("genres"), joined(q.genres, QLatin1Char('|')));
    if (q.isFavorite)
      query.addQueryItem(QStringLiteral("isFavorite"),
                         *q.isFavorite ? QStringLiteral("true") : QStringLiteral("false"));
    if (!q.albumArtistIds.isEmpty())
      query.addQueryItem(QStringLiteral("albumArtistIds"), joined(q.albumArtistIds));
    if (!q.artistIds.isEmpty()) query.addQueryItem(QStringLiteral("artistIds"), joined(q.artistIds));
    query.addQueryItem(QStringLiteral("startIndex"), QString::number(q.startIndex));
    query.addQueryItem(QStringLiteral("limit"), QString::number(q.limit));
    query.addQueryItem(QStringLiteral("fields"), MusicFields);
    query.addQueryItem(QStringLiteral("enableTotalRecordCount"), QStringLiteral("true"));
    return query;
  }

  void MusicApi::albums(const MusicQuery& q, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query = listQuery(q);
    query.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("MusicAlbum"));
    query.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt, context,
                      parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::songs(const MusicQuery& q, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query = listQuery(q);
    query.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("Audio"));
    query.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt, context,
                      parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::artists(const MusicQuery& q, bool all, QObject* context, Callback<ItemPage> done) {
    m_client.sendJson(JellyfinClient::Method::Get,
                      all ? QStringLiteral("Artists") : QStringLiteral("Artists/AlbumArtists"), listQuery(q),
                      std::nullopt, context, parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::genres(const MusicQuery& q, QObject* context, Callback<ItemPage> done) {
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("MusicGenres"), listQuery(q), std::nullopt,
                      context, parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::playlists(const MusicQuery& q, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query = listQuery(q);
    query.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("Playlist"));
    query.addQueryItem(QStringLiteral("mediaTypes"), QStringLiteral("Audio"));
    query.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt, context,
                      parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::albumTracks(const QString& albumId, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    query.addQueryItem(QStringLiteral("parentId"), albumId);
    query.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("Audio"));
    query.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("sortBy"), QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
    query.addQueryItem(QStringLiteral("fields"), MusicFields);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt, context,
                      parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::playlistTracks(const QString& playlistId, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    query.addQueryItem(QStringLiteral("fields"), MusicFields);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Playlists/%1/Items").arg(playlistId), query,
                      std::nullopt, context, parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::latestAlbums(const QString& parentId, int limit, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    if (!parentId.isEmpty()) query.addQueryItem(QStringLiteral("parentId"), parentId);
    query.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("MusicAlbum"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("fields"), MusicFields);
    m_client.send(JellyfinClient::Method::Get, QStringLiteral("Items/Latest"), query, std::nullopt, context,
                  [done = std::move(done)](Result<JellyfinClient::RawResponse> reply) {
                    if (!reply) return done(reply.error());
                    auto page = json::itemArray(QJsonDocument::fromJson(reply->body).array());
                    if (!page) return done(page.error());
                    done(std::move(page).value());
                  });
  }

  void MusicApi::recentlyPlayed(const QString& parentId, int limit, QObject* context, Callback<ItemPage> done) {
    MusicQuery q;
    q.parentId = parentId;
    q.sort = MusicSort::LastPlayed;
    q.descending = true;
    q.limit = limit;
    QUrlQuery query = listQuery(q);
    query.addQueryItem(QStringLiteral("includeItemTypes"), QStringLiteral("Audio"));
    query.addQueryItem(QStringLiteral("filters"), QStringLiteral("IsPlayed"));
    query.addQueryItem(QStringLiteral("recursive"), QStringLiteral("true"));
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Items"), query, std::nullopt, context,
                      parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::similarAlbums(const QString& albumId, int limit, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    query.addQueryItem(QStringLiteral("fields"), MusicFields);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("Albums/%1/Similar").arg(albumId), query,
                      std::nullopt, context, parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void MusicApi::createPlaylist(const QString& name, const QStringList& trackIds, QObject* context,
                                Callback<QString> done) {
    const QJsonObject body{{QStringLiteral("Name"), name},
                           {QStringLiteral("Ids"), QJsonArray::fromStringList(trackIds)},
                           {QStringLiteral("UserId"), m_client.userId()},
                           {QStringLiteral("MediaType"), QStringLiteral("Audio")}};
    m_client.sendJson(JellyfinClient::Method::Post, QStringLiteral("Playlists"), {}, QJsonDocument(body), context,
                      [done = std::move(done)](Result<QJsonObject> result) {
                        if (!result) return done(result.error());
                        const QString id = result.value().value(QLatin1String("Id")).toString();
                        if (id.isEmpty()) {
                          ApiError error;
                          error.kind = ApiError::Kind::InvalidResponse;
                          error.message = QStringLiteral("Playlist answer has no Id");
                          return done(error);
                        }
                        done(id);
                      });
  }

  void MusicApi::addToPlaylist(const QString& playlistId, const QStringList& trackIds, QObject* context,
                               Callback<Empty> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ids"), joined(trackIds));
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    m_client.sendNoContent(JellyfinClient::Method::Post, QStringLiteral("Playlists/%1/Items").arg(playlistId),
                           query, std::nullopt, context, std::move(done));
  }

  void MusicApi::removeFromPlaylist(const QString& playlistId, const QStringList& entryIds, QObject* context,
                                    Callback<Empty> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("entryIds"), joined(entryIds));
    m_client.sendNoContent(JellyfinClient::Method::Delete, QStringLiteral("Playlists/%1/Items").arg(playlistId),
                           query, std::nullopt, context, std::move(done));
  }

} // namespace jfd::api
