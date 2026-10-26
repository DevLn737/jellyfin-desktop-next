#pragma once

#include <jfd/api/Result.h>
#include <jfd/seerr/Models.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace jfd::seerr {

  template <class T> using Callback = std::function<void(api::Result<T>)>;

  // Why Seerr turned a request down, in the terms the user needs. The body of
  // an error answer is never kept or shown: it may carry addresses or names.
  enum class Refusal {
    SignedOut,    // 401: no session, or it ran out
    BadLogin,     // sign-in refused: wrong name or password, or no access
    NoPermission, // 403: the account may not do this, or a limit is used up
    AlreadyDone,  // 409: the title is already requested or available
    NotFound,
    Unreachable,  // network, timeout
    ServerError,  // 5xx
    Other,
  };
  Refusal refusalOf(const api::ApiError& error, bool signingIn = false);

  // Which seasons of a series to request.
  struct SeasonSelection {
    bool all = true;
    QList<int> numbers;
  };

  // HTTP client for one Seerr and one signed-in user. Sign-in is the user's
  // own Jellyfin account; Seerr answers with a session cookie, which is all
  // that is kept: the password lives only in the sign-in request, and no API
  // key is ever accepted or sent. Every request goes to the configured
  // address only and redirects are not followed, so neither the password nor
  // the cookie can reach another host. Callbacks run on this object's thread
  // and only while `context` is alive.
  class SeerrClient : public QObject {
    Q_OBJECT

  public:
    explicit SeerrClient(QObject* parent = nullptr);
    ~SeerrClient() override;

    // "host", "host:5055", "https://host/seerr/"; http is assumed without a
    // scheme. Credentials, queries and fragments are dropped.
    static std::optional<QUrl> normalizeServerUrl(const QString& input);
    // True for plain http to a host outside this computer and the local
    // network: a password sent there crosses the internet unencrypted.
    static bool isCleartextOverInternet(const QUrl& url);

    void setServerUrl(const QUrl& url) { m_serverUrl = url; }
    QUrl serverUrl() const { return m_serverUrl; }
    void setTimeoutMs(int timeoutMs) { m_timeoutMs = timeoutMs; }

    // Restores a saved session without contacting Seerr.
    void setSession(const QString& cookie, int userId);
    void clearSession();
    QString sessionCookie() const { return m_cookie; }
    int userId() const { return m_userId; }
    bool isSignedIn() const { return !m_cookie.isEmpty(); }

    // GET /status and /settings/public: is this a Seerr that uses Jellyfin.
    void probe(QObject* context, Callback<ServerInfo> done);
    // Probes first and sends the password only to a Seerr that uses Jellyfin.
    void signIn(const QString& userName, const QString& password, QObject* context,
                Callback<User> done);
    void currentUser(QObject* context, Callback<User> done);
    void signOut(QObject* context, Callback<api::Empty> done);

    void discoverMovies(int page, QObject* context, Callback<TitlePage> done);
    void discoverTv(int page, QObject* context, Callback<TitlePage> done);
    void trending(int page, QObject* context, Callback<TitlePage> done);
    void search(const QString& text, int page, QObject* context, Callback<TitlePage> done);

    void movie(int tmdbId, QObject* context, Callback<TitleDetails> done);
    void tv(int tmdbId, QObject* context, Callback<TitleDetails> done);

    // The signed-in user's own requests, newest first.
    void myRequests(int take, int skip, QObject* context, Callback<RequestPage> done);
    // POST /request with Seerr's defaults (no 4K, profile or folder).
    void request(MediaType type, int tmdbId, const SeasonSelection& seasons, QObject* context,
                 Callback<UserRequest> done);
    void cancelRequest(int requestId, QObject* context, Callback<api::Empty> done);

  Q_SIGNALS:
    void sessionChanged();
    // A call with a session was answered 401: the session was dropped.
    void sessionExpired();

  private:
    enum class Method { Get, Post, Delete };
    struct Raw {
      int status = 0;
      QByteArray body;
      QString setCookie;
    };

    QUrl url(const QString& path, const QUrlQuery& query = {}) const;
    void send(Method method, const QString& path, const QUrlQuery& query,
              const std::optional<QJsonDocument>& body, QObject* context, Callback<Raw> done);
    void sendJson(Method method, const QString& path, const QUrlQuery& query,
                  const std::optional<QJsonDocument>& body, QObject* context,
                  Callback<QJsonObject> done);
    void titlePage(const QString& path, QUrlQuery query, int page, QObject* context,
                   Callback<TitlePage> done);

    QNetworkAccessManager* m_network;
    QUrl m_serverUrl;
    QString m_cookie;
    int m_userId = 0;
    int m_timeoutMs = 15000;
  };

} // namespace jfd::seerr
