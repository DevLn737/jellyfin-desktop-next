#include <jfd/api/JellyfinClient.h>

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace jfd::api {

  namespace {
    const QString UserId = QStringLiteral("userId");

    // Fields every item list asks for; Jellyfin omits most of them otherwise.
    const QString ItemFields = QStringLiteral("Overview,MediaSourceCount,ProductionYear");

    ApiError httpError(int status, const QByteArray& body) {
      ApiError error;
      error.kind = ApiError::Kind::Http;
      error.httpStatus = status;
      const auto document = QJsonDocument::fromJson(body);
      if (document.isObject()) {
        error.problem = document.object();
        error.problemCode = error.problem.value(QLatin1String("code")).toString();
        error.message = error.problem.value(QLatin1String("detail")).toString();
        if (error.message.isEmpty())
          error.message = error.problem.value(QLatin1String("title")).toString();
      }
      if (error.message.isEmpty()) error.message = QStringLiteral("HTTP %1").arg(status);
      return error;
    }

    ApiError transportError(QNetworkReply* reply) {
      ApiError error;
      switch (reply->error()) {
      case QNetworkReply::OperationCanceledError:
        // Qt reports a transfer timeout as a cancellation.
        error.kind = reply->property("jfdTimedOut").toBool() ? ApiError::Kind::Timeout
                                                             : ApiError::Kind::Canceled;
        break;
      case QNetworkReply::TimeoutError:
        error.kind = ApiError::Kind::Timeout;
        break;
      default:
        error.kind = ApiError::Kind::Network;
        break;
      }
      error.message = reply->errorString();
      return error;
    }

    ApiError invalidResponse(const QString& message) {
      ApiError error;
      error.kind = ApiError::Kind::InvalidResponse;
      error.message = message;
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

    QStringList splitPathSegments(const QString& path) {
      return path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    }
  } // namespace

  JellyfinClient::JellyfinClient(ClientIdentity identity, QObject* parent)
      : QObject(parent), m_identity(std::move(identity)),
        m_network(new QNetworkAccessManager(this)) {
    // Media servers on a LAN often redirect http to https; follow only
    // redirects that keep or upgrade the scheme.
    m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
  }

  JellyfinClient::~JellyfinClient() = default;

  std::optional<QUrl> JellyfinClient::normalizeServerUrl(const QString& input) {
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
    // The web client's own address is a common paste; the API lives at the root.
    if (path.endsWith(QLatin1String("/web"), Qt::CaseInsensitive)) path.chop(4);
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    url.setUserInfo(QString());
    return url;
  }

  void JellyfinClient::setServerUrl(const QUrl& url) {
    m_serverUrl = url;
  }

  void JellyfinClient::setSession(const QString& accessToken, const QString& userId) {
    m_accessToken = accessToken;
    m_userId = userId;
    Q_EMIT sessionChanged();
  }

  void JellyfinClient::clearSession() {
    m_accessToken.clear();
    m_userId.clear();
    m_capabilities.reset();
    Q_EMIT sessionChanged();
  }

  QUrl JellyfinClient::url(const QString& path, const QUrlQuery& query) const {
    QUrl url = m_serverUrl;
    QStringList segments = splitPathSegments(m_serverUrl.path());
    segments += splitPathSegments(path);
    url.setPath(QLatin1Char('/') + segments.join(QLatin1Char('/')));
    url.setQuery(query);
    return url;
  }

  QByteArray JellyfinClient::authorizationHeader() const {
    return api::authorizationHeader(m_identity, m_accessToken);
  }

  QUrlQuery JellyfinClient::userQuery() const {
    QUrlQuery query;
    if (!m_userId.isEmpty()) query.addQueryItem(UserId, m_userId);
    return query;
  }

  void JellyfinClient::send(Method method, const QString& path, const QUrlQuery& query,
                            const std::optional<QJsonDocument>& body, QObject* context,
                            Callback<RawResponse> done) {
    QNetworkRequest request(url(path, query));
    request.setRawHeader("Authorization", authorizationHeader());
    request.setRawHeader("Accept", "application/json");
    if (m_timeoutMs > 0) request.setTransferTimeout(m_timeoutMs);

    QByteArray payload;
    if (body) payload = body->toJson(QJsonDocument::Compact);
    if (body || method == Method::Post)
      request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));

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

    const bool authenticated = !m_accessToken.isEmpty();
    const QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, guard, hasContext, authenticated, done = std::move(done)]() {
              reply->deleteLater();
              if (hasContext && guard.isNull()) return;

              if (reply->error() != QNetworkReply::NoError &&
                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isNull()) {
                done(transportError(reply));
                return;
              }
              const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
              const QByteArray data = reply->readAll();
              if (status < 200 || status > 299) {
                if (status == 401 && authenticated && !m_accessToken.isEmpty()) {
                  clearSession();
                  Q_EMIT sessionExpired();
                }
                done(httpError(status, data));
                return;
              }
              done(RawResponse{status, data});
            });
    // A transfer timeout surfaces as OperationCanceledError; tell it apart from
    // an abort on destruction by marking replies that ran out of time.
    if (m_timeoutMs > 0) {
      auto* timer = new QTimer(reply);
      timer->setSingleShot(true);
      connect(timer, &QTimer::timeout, reply,
              [reply]() { reply->setProperty("jfdTimedOut", true); });
      timer->start(m_timeoutMs > 50 ? m_timeoutMs - 50 : m_timeoutMs);
    }
  }

  void JellyfinClient::sendJson(Method method, const QString& path, const QUrlQuery& query,
                                const std::optional<QJsonDocument>& body, QObject* context,
                                Callback<QJsonObject> done) {
    send(method, path, query, body, context, [done = std::move(done)](Result<RawResponse> result) {
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

  void JellyfinClient::sendNoContent(Method method, const QString& path, const QUrlQuery& query,
                                     const std::optional<QJsonDocument>& body, QObject* context,
                                     Callback<Empty> done) {
    send(method, path, query, body, context, [done = std::move(done)](Result<RawResponse> result) {
      if (!result) {
        done(result.error());
        return;
      }
      done(Empty{});
    });
  }

  // --- Server ----------------------------------------------------------------

  void JellyfinClient::publicServerInfo(QObject* context, Callback<PublicServerInfo> done) {
    sendJson(Method::Get, QStringLiteral("System/Info/Public"), {}, std::nullopt, context,
             parsed<PublicServerInfo>(
                 std::move(done), [](const QJsonObject& o) -> Result<PublicServerInfo> {
                   auto info = json::publicServerInfo(o);
                   if (info && info->version < MinimumServerVersion) {
                     ApiError error;
                     error.kind = ApiError::Kind::Unsupported;
                     error.problemCode = QStringLiteral("ServerTooOld");
                     error.message = QStringLiteral("Jellyfin %1 is older than %2")
                                         .arg(info->versionText, MinimumServerVersion.toString());
                     return error;
                   }
                   return info;
                 }));
  }

  void JellyfinClient::quickConnectEnabled(QObject* context, Callback<bool> done) {
    send(Method::Get, QStringLiteral("QuickConnect/Enabled"), {}, std::nullopt, context,
         [done = std::move(done)](Result<RawResponse> result) {
           if (!result) {
             done(result.error());
             return;
           }
           const QByteArray body = result->body.trimmed();
           if (body == "true")
             done(true);
           else if (body == "false")
             done(false);
           else
             done(invalidResponse(QStringLiteral("QuickConnect/Enabled is not a boolean")));
         });
  }

  void JellyfinClient::probeSyncPlayPlaybackRate(QObject* context, Callback<bool> done) {
    // 0 is outside 0.25..5: the extension answers 400 (in a group) or 403
    // (authorization runs first, outside a group) and never applies it.
    const QJsonDocument body(QJsonObject{{QStringLiteral("PlaybackRate"), 0}});
    send(Method::Post, QStringLiteral("SyncPlay/SetPlaybackRate"), {}, body, context,
         [done = std::move(done)](Result<RawResponse> result) {
           if (result) {
             done(invalidResponse(QStringLiteral("SetPlaybackRate accepted a rate of 0")));
             return;
           }
           const ApiError& error = result.error();
           if (error.kind != ApiError::Kind::Http) {
             done(error);
             return;
           }
           switch (error.httpStatus) {
           case 400:
           case 403:
             done(true);
             break;
           case 404:
           case 405:
             done(false);
             break;
           default:
             done(error);
             break;
           }
         });
  }

  // --- Sign-in ---------------------------------------------------------------

  void JellyfinClient::storeSession(const AuthenticationResult& result) {
    m_accessToken = result.accessToken;
    m_userId = result.user.id;
    m_capabilities.reset();
    Q_EMIT sessionChanged();
  }

  void JellyfinClient::signIn(const QString& userName, const QString& password, QObject* context,
                              Callback<AuthenticationResult> done) {
    m_accessToken.clear();
    const QJsonDocument body(
        QJsonObject{{QStringLiteral("Username"), userName}, {QStringLiteral("Pw"), password}});
    sendJson(Method::Post, QStringLiteral("Users/AuthenticateByName"), {}, body, context,
             [this, done = std::move(done)](Result<QJsonObject> result) {
               if (!result) {
                 done(result.error());
                 return;
               }
               auto auth = json::authenticationResult(result.value());
               if (auth) storeSession(auth.value());
               done(std::move(auth));
             });
  }

  void JellyfinClient::quickConnectInitiate(QObject* context, Callback<QuickConnectState> done) {
    sendJson(Method::Post, QStringLiteral("QuickConnect/Initiate"), {}, std::nullopt, context,
             parsed<QuickConnectState>(std::move(done), json::quickConnectState));
  }

  void JellyfinClient::quickConnectState(const QString& secret, QObject* context,
                                         Callback<QuickConnectState> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("secret"), secret);
    sendJson(Method::Get, QStringLiteral("QuickConnect/Connect"), query, std::nullopt, context,
             parsed<QuickConnectState>(std::move(done), json::quickConnectState));
  }

  void JellyfinClient::quickConnectSignIn(const QString& secret, QObject* context,
                                          Callback<AuthenticationResult> done) {
    m_accessToken.clear();
    const QJsonDocument body(QJsonObject{{QStringLiteral("Secret"), secret}});
    sendJson(Method::Post, QStringLiteral("Users/AuthenticateWithQuickConnect"), {}, body, context,
             [this, done = std::move(done)](Result<QJsonObject> result) {
               if (!result) {
                 done(result.error());
                 return;
               }
               auto auth = json::authenticationResult(result.value());
               if (auth) storeSession(auth.value());
               done(std::move(auth));
             });
  }

  void JellyfinClient::signOut(QObject* context, Callback<Empty> done) {
    if (m_accessToken.isEmpty()) {
      done(Empty{});
      return;
    }
    sendNoContent(Method::Post, QStringLiteral("Sessions/Logout"), {}, std::nullopt, context,
                  [this, done = std::move(done)](Result<Empty> result) {
                    clearSession();
                    done(std::move(result));
                  });
  }

  void JellyfinClient::currentUser(QObject* context, Callback<UserInfo> done) {
    sendJson(Method::Get, QStringLiteral("Users/Me"), {}, std::nullopt, context,
             parsed<UserInfo>(std::move(done), json::user));
  }

  // --- Session ---------------------------------------------------------------

  void JellyfinClient::reportCapabilities(const SessionCapabilities& capabilities, QObject* context,
                                          Callback<Empty> done) {
    m_capabilities = capabilities;
    QJsonObject body{
        {QStringLiteral("PlayableMediaTypes"),
         QJsonArray::fromStringList(capabilities.playableMediaTypes)},
        {QStringLiteral("SupportedCommands"),
         QJsonArray::fromStringList(capabilities.supportedCommands)},
        {QStringLiteral("SupportsMediaControl"), capabilities.supportsMediaControl},
        {QStringLiteral("SupportsSyncPlayPlaybackRate"), capabilities.supportsSyncPlayPlaybackRate},
        {QStringLiteral("SupportsPersistentIdentifier"), true}};
    if (!capabilities.deviceProfile.isEmpty())
      body.insert(QStringLiteral("DeviceProfile"), capabilities.deviceProfile);
    sendNoContent(Method::Post, QStringLiteral("Sessions/Capabilities/Full"), {},
                  QJsonDocument(body), context, std::move(done));
  }

  // --- Items -----------------------------------------------------------------

  void JellyfinClient::items(const ItemQuery& q, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query = userQuery();
    if (!q.searchTerm.isEmpty()) query.addQueryItem(QStringLiteral("searchTerm"), q.searchTerm);
    if (!q.includeItemTypes.isEmpty())
      query.addQueryItem(QStringLiteral("includeItemTypes"),
                         q.includeItemTypes.join(QLatin1Char(',')));
    if (!q.parentId.isEmpty()) query.addQueryItem(QStringLiteral("parentId"), q.parentId);
    if (!q.sortBy.isEmpty()) query.addQueryItem(QStringLiteral("sortBy"), q.sortBy);
    query.addQueryItem(QStringLiteral("recursive"),
                       q.recursive ? QStringLiteral("true") : QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("startIndex"), QString::number(q.startIndex));
    query.addQueryItem(QStringLiteral("limit"), QString::number(q.limit));
    query.addQueryItem(QStringLiteral("fields"), ItemFields);
    query.addQueryItem(QStringLiteral("enableTotalRecordCount"), QStringLiteral("true"));
    sendJson(Method::Get, QStringLiteral("Items"), query, std::nullopt, context,
             parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void JellyfinClient::item(const QString& itemId, QObject* context, Callback<Item> done) {
    sendJson(Method::Get, QStringLiteral("Items/") + itemId, userQuery(), std::nullopt, context,
             parsed<Item>(std::move(done), json::item));
  }

  void JellyfinClient::seasons(const QString& seriesId, QObject* context, Callback<ItemPage> done) {
    QUrlQuery query = userQuery();
    query.addQueryItem(QStringLiteral("fields"), ItemFields);
    sendJson(Method::Get, QStringLiteral("Shows/%1/Seasons").arg(seriesId), query, std::nullopt,
             context, parsed<ItemPage>(std::move(done), json::itemPage));
  }

  void JellyfinClient::episodes(const QString& seriesId, const QString& seasonId, QObject* context,
                                Callback<ItemPage> done) {
    QUrlQuery query = userQuery();
    if (!seasonId.isEmpty()) query.addQueryItem(QStringLiteral("seasonId"), seasonId);
    query.addQueryItem(QStringLiteral("fields"), ItemFields);
    sendJson(Method::Get, QStringLiteral("Shows/%1/Episodes").arg(seriesId), query, std::nullopt,
             context, parsed<ItemPage>(std::move(done), json::itemPage));
  }

  QUrl JellyfinClient::primaryImageUrl(const Item& item, int maxWidth) const {
    if (item.primaryImageTag.isEmpty()) return {};
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tag"), item.primaryImageTag);
    query.addQueryItem(QStringLiteral("maxWidth"), QString::number(maxWidth));
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
    return url(QStringLiteral("Items/%1/Images/Primary").arg(item.id), query);
  }

  // --- Playback --------------------------------------------------------------

  void JellyfinClient::playbackInfo(PlaybackInfoRequest request, QObject* context,
                                    Callback<PlaybackInfo> done) {
    if (request.userId.isEmpty()) request.userId = m_userId;
    sendJson(Method::Post, QStringLiteral("Items/%1/PlaybackInfo").arg(request.itemId), {},
             QJsonDocument(playbackInfoBody(request)), context,
             parsed<PlaybackInfo>(std::move(done), json::playbackInfo));
  }

  StreamContext JellyfinClient::streamContext() const {
    return StreamContext{m_serverUrl, m_accessToken, m_identity.deviceId};
  }

  void JellyfinClient::reportPlaybackStart(const PlaybackProgress& progress, QObject* context,
                                           Callback<Empty> done) {
    sendNoContent(Method::Post, QStringLiteral("Sessions/Playing"), {},
                  QJsonDocument(playbackStartBody(progress)), context, std::move(done));
  }

  void JellyfinClient::reportPlaybackProgress(const PlaybackProgress& progress, QObject* context,
                                              Callback<Empty> done) {
    sendNoContent(Method::Post, QStringLiteral("Sessions/Playing/Progress"), {},
                  QJsonDocument(playbackProgressBody(progress)), context, std::move(done));
  }

  void JellyfinClient::reportPlaybackStopped(const PlaybackProgress& progress, bool failed,
                                             QObject* context, Callback<Empty> done) {
    sendNoContent(Method::Post, QStringLiteral("Sessions/Playing/Stopped"), {},
                  QJsonDocument(playbackStoppedBody(progress, failed)), context, std::move(done));
  }

  void JellyfinClient::markPlayed(const QString& itemId, QObject* context, Callback<Empty> done) {
    send(Method::Post, QStringLiteral("UserPlayedItems/") + itemId, userQuery(), std::nullopt,
         context, [done = std::move(done)](Result<RawResponse> result) {
           if (!result)
             done(result.error());
           else
             done(Empty{});
         });
  }

  void JellyfinClient::markUnplayed(const QString& itemId, QObject* context, Callback<Empty> done) {
    send(Method::Delete, QStringLiteral("UserPlayedItems/") + itemId, userQuery(), std::nullopt,
         context, [done = std::move(done)](Result<RawResponse> result) {
           if (!result)
             done(result.error());
           else
             done(Empty{});
         });
  }

} // namespace jfd::api
