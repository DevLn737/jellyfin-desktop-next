#pragma once

#include <jfd/api/ClientIdentity.h>
#include <jfd/api/Models.h>
#include <jfd/api/Playback.h>
#include <jfd/api/Result.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace jfd::api {

  template <class T> using Callback = std::function<void(Result<T>)>;

  // What the server needs to know to treat this client as a full player and
  // as a SyncPlay member with synchronized playback rate. Reports replace the
  // stored capabilities as a whole, so the client keeps the last value and
  // sends all of it again after every reconnect.
  struct SessionCapabilities {
    QStringList playableMediaTypes{QStringLiteral("Audio"), QStringLiteral("Video")};
    QStringList supportedCommands;
    bool supportsMediaControl = true;
    // Advertise only when the player really applies group rates.
    bool supportsSyncPlayPlaybackRate = false;
    QJsonObject deviceProfile;
  };

  struct ItemQuery {
    QString searchTerm;
    QStringList includeItemTypes;
    QString parentId;
    bool recursive = true;
    int startIndex = 0;
    int limit = 50;
    // Jellyfin ItemSortBy names, comma-separated: "SortName",
    // "DateCreated", "PremiereDate", "CommunityRating", "Random", ...
    QString sortBy;
    bool descending = false;
    QStringList genres;
    QList<int> years;
    std::optional<bool> isPlayed;
    std::optional<bool> isFavorite;
    QStringList ids;
  };

  // Image types the server keeps per item: "Primary", "Backdrop", "Thumb",
  // "Logo", "Banner". Sizes are in physical pixels; the server scales and
  // crops to fill them, so a card asks for its own size and no more.
  struct ImageRequest {
    QString itemId;
    QString type = QStringLiteral("Primary");
    // Backdrops are numbered; other types have one image.
    std::optional<int> index;
    // The image's tag: changes when the image does, so the URL can be cached
    // for as long as the tag stays the same.
    QString tag;
    int fillWidth = 0;
    int fillHeight = 0;
    int maxWidth = 0;
    int quality = 90;
  };

  // HTTP client for one server and, once signed in, one user. Calls are
  // asynchronous; each callback runs on the thread that owns this object and
  // only while `context` is alive, so a screen that goes away never receives
  // a late answer.
  class JellyfinClient : public QObject {
    Q_OBJECT

  public:
    struct RawResponse {
      int httpStatus = 0;
      QByteArray body;
    };

    explicit JellyfinClient(ClientIdentity identity, QObject* parent = nullptr);
    ~JellyfinClient() override;

    // Accepts "host", "host:8096", "http://host/jellyfin/" and similar.
    // Without a scheme, http is assumed.
    static std::optional<QUrl> normalizeServerUrl(const QString& input);

    void setServerUrl(const QUrl& url);
    QUrl serverUrl() const { return m_serverUrl; }
    const ClientIdentity& identity() const { return m_identity; }

    // Restores a saved sign-in without contacting the server.
    void setSession(const QString& accessToken, const QString& userId);
    void clearSession();
    QString accessToken() const { return m_accessToken; }
    QString userId() const { return m_userId; }
    bool isSignedIn() const { return !m_accessToken.isEmpty(); }

    // Per-request timeout; 0 disables it.
    void setTimeoutMs(int timeoutMs) { m_timeoutMs = timeoutMs; }

    // Forgets pooled connections, so the next requests open new ones. After
    // the computer sleeps, the pooled ones are usually dead without knowing.
    void dropConnections();

    QUrl url(const QString& path, const QUrlQuery& query = {}) const;

    // --- Server --------------------------------------------------------------

    // GET System/Info/Public. Fails with Unsupported when the server is older
    // than MinimumServerVersion.
    void publicServerInfo(QObject* context, Callback<PublicServerInfo> done);
    void quickConnectEnabled(QObject* context, Callback<bool> done);
    // Whether the server has the SyncPlay playback-rate extension. Probes
    // POST SyncPlay/SetPlaybackRate with an out-of-range rate, which the
    // extension rejects (403 outside a group, 400 inside one) without changing
    // anything, while a stock server has no such route (404/405).
    void probeSyncPlayPlaybackRate(QObject* context, Callback<bool> done);

    // --- Sign-in -------------------------------------------------------------

    // POST Users/AuthenticateByName. On success the session is stored.
    void signIn(const QString& userName, const QString& password, QObject* context,
                Callback<AuthenticationResult> done);
    // POST QuickConnect/Initiate: returns the code to show to the user.
    void quickConnectInitiate(QObject* context, Callback<QuickConnectState> done);
    // GET QuickConnect/Connect: poll until `authenticated`.
    void quickConnectState(const QString& secret, QObject* context,
                           Callback<QuickConnectState> done);
    // POST Users/AuthenticateWithQuickConnect. On success the session is stored.
    void quickConnectSignIn(const QString& secret, QObject* context,
                            Callback<AuthenticationResult> done);
    // POST Sessions/Logout: revokes the token on the server, then clears the
    // local session whatever the answer.
    void signOut(QObject* context, Callback<Empty> done);
    void currentUser(QObject* context, Callback<UserInfo> done);

    // --- Session -------------------------------------------------------------

    // POST Sessions/Capabilities/Full and remember the value for reconnects.
    void reportCapabilities(const SessionCapabilities& capabilities, QObject* context,
                            Callback<Empty> done);
    const std::optional<SessionCapabilities>& capabilities() const { return m_capabilities; }

    // --- Items ---------------------------------------------------------------

    void items(const ItemQuery& query, QObject* context, Callback<ItemPage> done);
    void item(const QString& itemId, QObject* context, Callback<Item> done);
    void seasons(const QString& seriesId, QObject* context, Callback<ItemPage> done);
    void episodes(const QString& seriesId, const QString& seasonId, QObject* context,
                  Callback<ItemPage> done);
    // Primary image of an item; images need no authentication.
    QUrl primaryImageUrl(const Item& item, int maxWidth) const;
    QUrl imageUrl(const ImageRequest& request) const;

    // GET Items/{id} with the fields a detail page shows, trickplay included.
    void itemDetails(const QString& itemId, QObject* context, Callback<Item> done);
    // GET UserViews: the user's libraries, in the order the user set.
    void views(QObject* context, Callback<ItemPage> done);
    // GET UserItems/Resume: partly watched videos, most recent first.
    // `parentId` limits it to one library.
    void resumeItems(int limit, const QString& parentId, QObject* context, Callback<ItemPage> done);
    // GET Shows/NextUp: the next episode of each series in progress.
    void nextUp(int limit, const QString& parentId, QObject* context, Callback<ItemPage> done);
    // GET Shows/NextUp for one series, resumable episodes included: the
    // episode "Play" on the series page starts.
    void nextUpForSeries(const QString& seriesId, QObject* context, Callback<ItemPage> done);
    // GET Items/Latest for one library. Episodes are grouped into their
    // series, as in Jellyfin Web.
    void latestItems(const QString& parentId, int limit, QObject* context, Callback<ItemPage> done);
    void similarItems(const QString& itemId, int limit, QObject* context, Callback<ItemPage> done);
    // The ids of a collection's (BoxSet's) items, without images or user
    // data. The server lists a collection's items but not an item's
    // collections, so this is how the collections holding an item are found.
    void collectionItemIds(const QString& collectionId, QObject* context,
                           Callback<QStringList> done);
    // GET Items/Filters: genres, years and ratings present in a library.
    void itemFilters(const QString& parentId, const QStringList& includeItemTypes, QObject* context,
                     Callback<ItemFilters> done);
    // POST/DELETE UserFavoriteItems/{id}; answers the item's new user data.
    void setFavorite(const QString& itemId, bool favorite, QObject* context,
                     Callback<UserItemData> done);

    // GET MediaSegments/{id}: intros, recaps, outros, sorted by start. Empty
    // when the server has no segment provider; an unknown item is NotFound.
    void mediaSegments(const QString& itemId, QObject* context, Callback<QList<MediaSegment>> done);
    // A trickplay tile (see TrickplayInfo). Unlike images, tiles need the
    // session's authorization, so fetch them with authorizedGet() or a
    // loader that sends authorizationHeader().
    QUrl trickplayTileUrl(const QString& itemId, const QString& mediaSourceId, int width,
                          int tileIndex) const;
    // GET of an absolute URL on this server (an image or a trickplay tile)
    // with the session's authorization; the body is returned as is.
    void authorizedGet(const QUrl& target, QObject* context, Callback<RawResponse> done);

    // --- Playback ------------------------------------------------------------

    // POST Items/{id}/PlaybackInfo. `request.userId` defaults to the signed-in user.
    void playbackInfo(PlaybackInfoRequest request, QObject* context, Callback<PlaybackInfo> done);
    StreamContext streamContext() const;

    void reportPlaybackStart(const PlaybackProgress& progress, QObject* context,
                             Callback<Empty> done);
    void reportPlaybackProgress(const PlaybackProgress& progress, QObject* context,
                                Callback<Empty> done);
    void reportPlaybackStopped(const PlaybackProgress& progress, bool failed, QObject* context,
                               Callback<Empty> done);
    void markPlayed(const QString& itemId, QObject* context, Callback<Empty> done);
    void markUnplayed(const QString& itemId, QObject* context, Callback<Empty> done);

    // --- Raw requests --------------------------------------------------------

    enum class Method { Get, Post, Delete };

    // Sends a request with this client's authorization. Non-2xx answers fail
    // with an Http error carrying ProblemDetails when the body has them.
    void send(Method method, const QString& path, const QUrlQuery& query,
              const std::optional<QJsonDocument>& body, QObject* context,
              Callback<RawResponse> done);
    void sendJson(Method method, const QString& path, const QUrlQuery& query,
                  const std::optional<QJsonDocument>& body, QObject* context,
                  Callback<QJsonObject> done);
    void sendNoContent(Method method, const QString& path, const QUrlQuery& query,
                       const std::optional<QJsonDocument>& body, QObject* context,
                       Callback<Empty> done);

    // For callers that build requests and parse answers themselves, such as
    // the SyncPlay group client's transport. `pathAndQuery` is relative to the
    // server URL ("/SyncPlay/Join", "/Items?ids=1"); a non-empty body is sent
    // as JSON. `done` gets the HTTP status and body of any answer, errors
    // included, or status 0 when no answer arrived (network error, timeout).
    // A 401 on a signed-in client still clears the session.
    void rawRequest(Method method, const QString& pathAndQuery, const QByteArray& jsonBody,
                    QObject* context, std::function<void(int status, QByteArray body)> done);

    QByteArray authorizationHeader() const;

  Q_SIGNALS:
    // The server rejected the stored token (401). The session is cleared.
    void sessionExpired();
    void sessionChanged();

  private:
    void finishRaw(QNetworkReply* reply, QObject* context, Callback<RawResponse> done);
    void storeSession(const AuthenticationResult& result);
    QNetworkReply* startRequest(Method method, const QUrl& target, const QByteArray* payload);
    void expireSessionOn401(int status, bool wasAuthenticated);
    QUrlQuery userQuery() const;

    ClientIdentity m_identity;
    QNetworkAccessManager* m_network = nullptr;
    QUrl m_serverUrl;
    QString m_accessToken;
    QString m_userId;
    int m_timeoutMs = 30'000;
    std::optional<SessionCapabilities> m_capabilities;
  };

} // namespace jfd::api
