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
    QString sortBy;
  };

  // HTTP client for one server and, once signed in, one user. Calls are
  // asynchronous; each callback runs on the thread that owns this object and
  // only while `context` is alive, so a screen that goes away never receives
  // a late answer.
  class JellyfinClient : public QObject {
    Q_OBJECT

  public:
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

    struct RawResponse {
      int httpStatus = 0;
      QByteArray body;
    };

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

    QByteArray authorizationHeader() const;

  Q_SIGNALS:
    // The server rejected the stored token (401). The session is cleared.
    void sessionExpired();
    void sessionChanged();

  private:
    void storeSession(const AuthenticationResult& result);
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
