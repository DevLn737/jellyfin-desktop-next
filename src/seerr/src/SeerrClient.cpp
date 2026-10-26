#include <jfd/seerr/SeerrClient.h>

#include <QHostAddress>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace jfd::seerr {

  using api::ApiError;
  using api::Empty;
  using api::Result;

  namespace {
    const QString SessionCookieName = QStringLiteral("connect.sid");

    ApiError httpError(int status) {
      ApiError error;
      error.kind = ApiError::Kind::Http;
      error.httpStatus = status;
      error.message = QStringLiteral("HTTP %1").arg(status);
      return error;
    }

    ApiError transportError(QNetworkReply* reply) {
      ApiError error;
      switch (reply->error()) {
      case QNetworkReply::OperationCanceledError:
      case QNetworkReply::TimeoutError:
        error.kind = ApiError::Kind::Timeout;
        break;
      default:
        error.kind = ApiError::Kind::Network;
        break;
      }
      // Qt's text can contain the address; keep only the kind.
      error.message = QStringLiteral("Seerr could not be reached");
      return error;
    }

    ApiError invalidResponse(const QString& message) {
      ApiError error;
      error.kind = ApiError::Kind::InvalidResponse;
      error.message = message;
      return error;
    }

    ApiError notJellyfin() {
      ApiError error;
      error.kind = ApiError::Kind::Unsupported;
      error.message = QStringLiteral("This Seerr does not use Jellyfin");
      return error;
    }

    template <class T, class Parse> Callback<QJsonObject> parsed(Callback<T> done, Parse parse) {
      return [done = std::move(done), parse](Result<QJsonObject> result) {
        if (!result) {
          done(result.error());
          return;
        }
        done(parse(result.value()));
      };
    }

    bool isPrivateHost(const QString& host) {
      if (host.isEmpty() || (!host.contains(QLatin1Char('.')) && !host.contains(QLatin1Char(':'))))
        return true; // single-label names are local
      const QString lower = host.toLower();
      for (const char* suffix : {".local", ".lan", ".home.arpa", ".internal", ".localdomain"}) {
        if (lower.endsWith(QLatin1String(suffix))) return true;
      }
      QHostAddress address;
      if (!address.setAddress(host)) return false;
      if (address.isLoopback()) return true;
      if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v4 = address.toIPv4Address();
        return (v4 >> 24) == 10 || (v4 >> 20) == 0xAC1 /* 172.16/12 */ || (v4 >> 16) == 0xC0A8 ||
               (v4 >> 16) == 0xA9FE /* 169.254/16 */;
      }
      const Q_IPV6ADDR v6 = address.toIPv6Address();
      return (v6[0] & 0xFE) == 0xFC /* fc00::/7 */ || (v6[0] == 0xFE && (v6[1] & 0xC0) == 0x80);
    }
  } // namespace

  Refusal refusalOf(const ApiError& error, bool signingIn) {
    switch (error.kind) {
    case ApiError::Kind::Network:
    case ApiError::Kind::Timeout:
      return Refusal::Unreachable;
    case ApiError::Kind::Http:
      if (error.httpStatus == 401) return signingIn ? Refusal::BadLogin : Refusal::SignedOut;
      if (error.httpStatus == 403) return signingIn ? Refusal::BadLogin : Refusal::NoPermission;
      if (error.httpStatus == 404) return Refusal::NotFound;
      if (error.httpStatus == 409) return Refusal::AlreadyDone;
      if (error.httpStatus >= 500) return signingIn ? Refusal::BadLogin : Refusal::ServerError;
      return Refusal::Other;
    default:
      return Refusal::Other;
    }
  }

  SeerrClient::SeerrClient(QObject* parent)
      : QObject(parent), m_network(new QNetworkAccessManager(this)) {
    // A redirect may point at another host; the password and the cookie must
    // not follow it. The answer is reported as it is (a 3xx), and the user
    // fixes the address (usually http instead of https).
    m_network->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    // No cookie jar: the only cookie is the one this client keeps and sends.
    m_network->setCookieJar(nullptr);
  }

  SeerrClient::~SeerrClient() = default;

  std::optional<QUrl> SeerrClient::normalizeServerUrl(const QString& input) {
    QString text = input.trimmed();
    if (text.isEmpty()) return std::nullopt;
    if (!text.contains(QLatin1String("://"))) text.prepend(QLatin1String("http://"));
    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
      return std::nullopt;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
      path.chop(1);
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    url.setUserInfo(QString());
    return url;
  }

  bool SeerrClient::isCleartextOverInternet(const QUrl& url) {
    return url.scheme() == QLatin1String("http") && !isPrivateHost(url.host());
  }

  void SeerrClient::setSession(const QString& cookie, int userId) {
    m_cookie = cookie;
    m_userId = userId;
    Q_EMIT sessionChanged();
  }

  void SeerrClient::clearSession() {
    m_cookie.clear();
    m_userId = 0;
    Q_EMIT sessionChanged();
  }

  QUrl SeerrClient::url(const QString& path, const QUrlQuery& query) const {
    QUrl target = m_serverUrl;
    QString base = m_serverUrl.path();
    while (base.endsWith(QLatin1Char('/')))
      base.chop(1);
    target.setPath(base + QStringLiteral("/api/v1") + path);
    target.setQuery(query);
    return target;
  }

  void SeerrClient::send(Method method, const QString& path, const QUrlQuery& query,
                         const std::optional<QJsonDocument>& body, QObject* context,
                         Callback<Raw> done) {
    QNetworkRequest request(url(path, query));
    request.setRawHeader("Accept", "application/json");
    if (!m_cookie.isEmpty())
      request.setRawHeader("Cookie", (SessionCookieName + QLatin1Char('=') + m_cookie).toUtf8());
    if (m_timeoutMs > 0) request.setTransferTimeout(m_timeoutMs);
    QByteArray payload;
    if (body) {
      payload = body->toJson(QJsonDocument::Compact);
      request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    }

    QNetworkReply* reply = nullptr;
    switch (method) {
    case Method::Get:
      reply = m_network->get(request);
      break;
    case Method::Post:
      reply = m_network->post(request, payload);
      break;
    case Method::Delete:
      reply = m_network->deleteResource(request);
      break;
    }

    const bool authenticated = !m_cookie.isEmpty();
    const QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, guard, hasContext, authenticated, done = std::move(done)]() {
              reply->deleteLater();
              if (hasContext && guard.isNull()) return;
              if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isNull()) {
                done(transportError(reply));
                return;
              }
              const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
              if (status < 200 || status > 299) {
                if (status == 401 && authenticated && !m_cookie.isEmpty()) {
                  clearSession();
                  Q_EMIT sessionExpired();
                }
                done(httpError(status));
                return;
              }
              Raw raw;
              raw.status = status;
              raw.body = reply->readAll();
              for (const QNetworkCookie& cookie :
                   QNetworkCookie::parseCookies(reply->rawHeader("Set-Cookie"))) {
                if (QString::fromUtf8(cookie.name()) == SessionCookieName)
                  raw.setCookie = QString::fromUtf8(cookie.value());
              }
              done(std::move(raw));
            });
  }

  void SeerrClient::sendJson(Method method, const QString& path, const QUrlQuery& query,
                             const std::optional<QJsonDocument>& body, QObject* context,
                             Callback<QJsonObject> done) {
    send(method, path, query, body, context, [done = std::move(done)](Result<Raw> result) {
      if (!result) {
        done(result.error());
        return;
      }
      QJsonParseError parseError{};
      const auto document = QJsonDocument::fromJson(result->body, &parseError);
      if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        done(invalidResponse(QStringLiteral("Expected a JSON object")));
        return;
      }
      done(document.object());
    });
  }

  // --- Server and sign-in ----------------------------------------------------

  void SeerrClient::probe(QObject* context, Callback<ServerInfo> done) {
    sendJson(Method::Get, QStringLiteral("/status"), {}, std::nullopt, context,
             [this, context, done = std::move(done)](Result<QJsonObject> status) mutable {
               if (!status) {
                 done(status.error());
                 return;
               }
               sendJson(Method::Get, QStringLiteral("/settings/public"), {}, std::nullopt, context,
                        [status = status.value(), done = std::move(done)](Result<QJsonObject> pub) {
                          if (!pub) {
                            done(pub.error());
                            return;
                          }
                          const auto info = json::serverInfo(status, pub.value());
                          if (!info) {
                            done(invalidResponse(QStringLiteral("Not a Seerr")));
                            return;
                          }
                          done(*info);
                        });
             });
  }

  void SeerrClient::signIn(const QString& userName, const QString& password, QObject* context,
                           Callback<User> done) {
    probe(context, [this, userName, password, context, done = std::move(done)](
                       Result<ServerInfo> info) mutable {
      if (!info) {
        done(info.error());
        return;
      }
      if (!info->usesJellyfin()) {
        done(notJellyfin());
        return;
      }
      QJsonObject body;
      body.insert(QStringLiteral("username"), userName);
      body.insert(QStringLiteral("password"), password);
      send(Method::Post, QStringLiteral("/auth/jellyfin"), {}, QJsonDocument(body), context,
           [this, done = std::move(done)](Result<Raw> raw) {
             if (!raw) {
               done(raw.error());
               return;
             }
             QJsonParseError parseError{};
             const auto document = QJsonDocument::fromJson(raw->body, &parseError);
             if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
                 raw->setCookie.isEmpty()) {
               done(invalidResponse(QStringLiteral("Unexpected sign-in answer")));
               return;
             }
             // Only the id, name, rights and count are read: the answer also
             // carries the Jellyfin token of this user, which is dropped here.
             const User user = json::user(document.object());
             if (user.id <= 0) {
               done(invalidResponse(QStringLiteral("Unexpected sign-in answer")));
               return;
             }
             setSession(raw->setCookie, user.id);
             done(user);
           });
    });
  }

  void SeerrClient::currentUser(QObject* context, Callback<User> done) {
    sendJson(Method::Get, QStringLiteral("/auth/me"), {}, std::nullopt, context,
             parsed<User>(std::move(done), [this](const QJsonObject& o) -> Result<User> {
               const User user = json::user(o);
               if (user.id <= 0) return invalidResponse(QStringLiteral("Unexpected user"));
               m_userId = user.id;
               return user;
             }));
  }

  void SeerrClient::signOut(QObject* context, Callback<Empty> done) {
    send(Method::Post, QStringLiteral("/auth/logout"), {}, std::nullopt, context,
         [this, done = std::move(done)](Result<Raw>) {
           // The local session goes whatever the answer.
           clearSession();
           done(Empty{});
         });
  }

  // --- Browse ----------------------------------------------------------------

  void SeerrClient::titlePage(const QString& path, QUrlQuery query, int page, QObject* context,
                              Callback<TitlePage> done) {
    query.addQueryItem(QStringLiteral("page"), QString::number(page < 1 ? 1 : page));
    sendJson(Method::Get, path, query, std::nullopt, context,
             parsed<TitlePage>(std::move(done), [](const QJsonObject& o) -> Result<TitlePage> {
               return json::titlePage(o);
             }));
  }

  void SeerrClient::discoverMovies(int page, QObject* context, Callback<TitlePage> done) {
    titlePage(QStringLiteral("/discover/movies"), {}, page, context, std::move(done));
  }

  void SeerrClient::discoverTv(int page, QObject* context, Callback<TitlePage> done) {
    titlePage(QStringLiteral("/discover/tv"), {}, page, context, std::move(done));
  }

  void SeerrClient::trending(int page, QObject* context, Callback<TitlePage> done) {
    titlePage(QStringLiteral("/discover/trending"), {}, page, context, std::move(done));
  }

  void SeerrClient::search(const QString& text, int page, QObject* context,
                           Callback<TitlePage> done) {
    QUrlQuery query;
    // Seerr wants the text percent-encoded, spaces included.
    query.addQueryItem(QStringLiteral("query"),
                       QString::fromLatin1(QUrl::toPercentEncoding(text.trimmed())));
    titlePage(QStringLiteral("/search"), query, page, context, std::move(done));
  }

  void SeerrClient::movie(int tmdbId, QObject* context, Callback<TitleDetails> done) {
    sendJson(Method::Get, QStringLiteral("/movie/%1").arg(tmdbId), {}, std::nullopt, context,
             parsed<TitleDetails>(std::move(done), [](const QJsonObject& o) -> Result<TitleDetails> {
               return json::details(o, MediaType::Movie);
             }));
  }

  void SeerrClient::tv(int tmdbId, QObject* context, Callback<TitleDetails> done) {
    sendJson(Method::Get, QStringLiteral("/tv/%1").arg(tmdbId), {}, std::nullopt, context,
             parsed<TitleDetails>(std::move(done), [](const QJsonObject& o) -> Result<TitleDetails> {
               return json::details(o, MediaType::Tv);
             }));
  }

  // --- Requests --------------------------------------------------------------

  void SeerrClient::myRequests(int take, int skip, QObject* context, Callback<RequestPage> done) {
    if (m_userId <= 0) {
      ApiError error = httpError(401);
      done(error);
      return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("take"), QString::number(take));
    query.addQueryItem(QStringLiteral("skip"), QString::number(skip));
    sendJson(Method::Get, QStringLiteral("/user/%1/requests").arg(m_userId), query, std::nullopt,
             context,
             parsed<RequestPage>(std::move(done), [](const QJsonObject& o) -> Result<RequestPage> {
               return json::requestPage(o);
             }));
  }

  void SeerrClient::request(MediaType type, int tmdbId, const SeasonSelection& seasons,
                            QObject* context, Callback<UserRequest> done) {
    QJsonObject body;
    body.insert(QStringLiteral("mediaType"),
                type == MediaType::Movie ? QStringLiteral("movie") : QStringLiteral("tv"));
    body.insert(QStringLiteral("mediaId"), tmdbId);
    if (type == MediaType::Tv) {
      if (seasons.all) {
        body.insert(QStringLiteral("seasons"), QStringLiteral("all"));
      } else {
        QJsonArray numbers;
        for (int n : seasons.numbers)
          numbers.append(n);
        body.insert(QStringLiteral("seasons"), numbers);
      }
    }
    sendJson(Method::Post, QStringLiteral("/request"), {}, QJsonDocument(body), context,
             parsed<UserRequest>(std::move(done), [](const QJsonObject& o) -> Result<UserRequest> {
               const UserRequest request = json::userRequest(o);
               if (request.id <= 0) return invalidResponse(QStringLiteral("Unexpected request"));
               return request;
             }));
  }

  void SeerrClient::cancelRequest(int requestId, QObject* context, Callback<Empty> done) {
    send(Method::Delete, QStringLiteral("/request/%1").arg(requestId), {}, std::nullopt, context,
         [done = std::move(done)](Result<Raw> raw) {
           if (!raw) {
             done(raw.error());
             return;
           }
           done(Empty{});
         });
  }

} // namespace jfd::seerr
