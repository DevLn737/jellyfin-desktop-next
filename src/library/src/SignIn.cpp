#include <jfd/library/SignIn.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/ServerDiscovery.h>
#include <jfd/platform/CredentialStore.h>

#include <QVariantMap>

namespace jfd::library {

  SignIn::SignIn(api::JellyfinClient& client, ServerStore& servers,
                 platform::CredentialStore& tokens, QObject* parent)
      : QObject(parent), m_client(client), m_servers(servers), m_tokens(tokens),
        m_discovery(new api::ServerDiscovery(this)) {
    m_poll.setInterval(3000);
    connect(&m_poll, &QTimer::timeout, this, &SignIn::pollQuickConnect);
    connect(m_discovery, &api::ServerDiscovery::serverFound, this, &SignIn::discoveredChanged);
    connect(m_discovery, &api::ServerDiscovery::finished, this, &SignIn::discoveringChanged);
    connect(&m_client, &api::JellyfinClient::sessionExpired, this, &SignIn::onSessionExpired);
  }

  SignIn::~SignIn() = default;

  bool SignIn::busy() const {
    return m_step == Step::CheckingServer || m_step == Step::SigningIn;
  }

  bool SignIn::discovering() const {
    return m_discovery->isRunning();
  }

  QVariantList SignIn::discoveredServers() const {
    QVariantList out;
    for (const api::DiscoveredServer& s : m_discovery->servers())
      out.append(QVariantMap{{QStringLiteral("id"), s.id},
                             {QStringLiteral("name"), s.name},
                             {QStringLiteral("address"), s.address.toString()}});
    return out;
  }

  QVariantList SignIn::savedServers() const {
    QVariantList out;
    for (const SavedServer& s : m_servers.servers())
      out.append(QVariantMap{{QStringLiteral("id"), s.id},
                             {QStringLiteral("name"), s.name},
                             {QStringLiteral("address"), s.url.toString()},
                             {QStringLiteral("userName"), s.userName}});
    return out;
  }

  void SignIn::setStep(Step step) {
    if (m_step == step) return;
    m_step = step;
    Q_EMIT stepChanged();
  }

  void SignIn::setError(const QString& text) {
    if (m_errorText == text) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

  QString SignIn::describe(const api::ApiError& error) const {
    using Kind = api::ApiError::Kind;
    switch (error.kind) {
    case Kind::Network:
      return tr("The server cannot be reached. Check the address and the network.");
    case Kind::Timeout:
      return tr("The server did not answer in time.");
    case Kind::Unsupported:
      if (error.problemCode == QLatin1String("ServerTooOld"))
        return tr("This server is too old: %1").arg(error.message);
      return error.message;
    case Kind::InvalidResponse:
      return tr("This address does not answer like a Jellyfin server.");
    case Kind::Http:
      if (error.httpStatus == 401) return tr("Wrong user name or password.");
      if (error.httpStatus == 403) return tr("This user is not allowed to sign in.");
      return tr("The server refused the request (%1).").arg(error.message);
    case Kind::Canceled:
      return {};
    }
    return error.message;
  }

  // --- Server choice ---------------------------------------------------------

  void SignIn::discover() {
    m_discovery->start();
    Q_EMIT discoveredChanged();
    Q_EMIT discoveringChanged();
  }

  void SignIn::connectToServer(const QString& address) {
    const auto url = api::JellyfinClient::normalizeServerUrl(address);
    if (!url) {
      setError(tr("This is not a server address."));
      return;
    }
    checkServer(*url, {});
  }

  void SignIn::chooseSavedServer(const QString& serverId) {
    const auto saved = m_servers.server(serverId);
    if (!saved) return;
    m_servers.setActive(serverId);
    Q_EMIT savedChanged();
    if (!saved->userId.isEmpty() && restore()) return;
    checkServer(saved->url, saved->userName);
  }

  void SignIn::forgetServer(const QString& serverId) {
    const auto saved = m_servers.server(serverId);
    if (!saved) return;
    if (!saved->userId.isEmpty())
      m_tokens.remove(platform::accessTokenKey(saved->id, saved->userId));
    m_servers.remove(serverId);
    Q_EMIT savedChanged();
  }

  void SignIn::checkServer(const QUrl& url, const QString& preferredUserName) {
    const quint64 attempt = ++m_attempt;
    m_poll.stop();
    setError({});
    m_client.clearSession();
    m_client.setServerUrl(url);
    m_server = SavedServer{};
    m_server.url = url;
    m_userName = preferredUserName;
    Q_EMIT serverChanged();
    setStep(Step::CheckingServer);
    m_client.publicServerInfo(this, [this, attempt](api::Result<api::PublicServerInfo> info) {
      if (attempt != m_attempt) return;
      if (!info) {
        setError(describe(info.error()));
        setStep(Step::ChooseServer);
        return;
      }
      m_server.id = info->id;
      m_server.name = info->name;
      m_client.quickConnectEnabled(this, [this, attempt](api::Result<bool> enabled) {
        if (attempt != m_attempt) return;
        m_quickConnect = enabled.ok() && enabled.value();
        Q_EMIT serverChanged();
        setStep(Step::Credentials);
      });
    });
  }

  void SignIn::back() {
    ++m_attempt;
    m_poll.stop();
    m_client.clearSession();
    setError({});
    setStep(Step::ChooseServer);
  }

  // --- Sign-in ---------------------------------------------------------------

  void SignIn::signIn(const QString& userName, const QString& password) {
    if (m_step != Step::Credentials) return;
    if (userName.trimmed().isEmpty()) {
      setError(tr("Enter a user name."));
      return;
    }
    const quint64 attempt = ++m_attempt;
    setError({});
    setStep(Step::SigningIn);
    m_client.signIn(userName.trimmed(), password, this,
                    [this, attempt](api::Result<api::AuthenticationResult> result) {
                      if (attempt != m_attempt) return;
                      if (!result) {
                        setError(describe(result.error()));
                        setStep(Step::Credentials);
                        return;
                      }
                      finishSignIn(result.value());
                    });
  }

  void SignIn::startQuickConnect() {
    if (m_step != Step::Credentials || !m_quickConnect) return;
    const quint64 attempt = ++m_attempt;
    setError({});
    setStep(Step::QuickConnect);
    m_client.quickConnectInitiate(this, [this, attempt](api::Result<api::QuickConnectState> state) {
      if (attempt != m_attempt) return;
      if (!state) {
        setError(describe(state.error()));
        setStep(Step::Credentials);
        return;
      }
      m_quickConnectSecret = state->secret;
      m_quickConnectCode = state->code;
      Q_EMIT quickConnectCodeChanged();
      m_poll.start();
    });
  }

  void SignIn::pollQuickConnect() {
    const quint64 attempt = m_attempt;
    m_client.quickConnectState(
        m_quickConnectSecret, this, [this, attempt](api::Result<api::QuickConnectState> state) {
          if (attempt != m_attempt || m_step != Step::QuickConnect) return;
          if (!state) {
            // The code expired or the server stopped it.
            m_poll.stop();
            setError(describe(state.error()));
            setStep(Step::Credentials);
            return;
          }
          if (!state->authenticated) return;
          m_poll.stop();
          setStep(Step::SigningIn);
          m_client.quickConnectSignIn(
              m_quickConnectSecret, this,
              [this, attempt](api::Result<api::AuthenticationResult> result) {
                if (attempt != m_attempt) return;
                if (!result) {
                  setError(describe(result.error()));
                  setStep(Step::Credentials);
                  return;
                }
                finishSignIn(result.value());
              });
        });
  }

  void SignIn::cancelQuickConnect() {
    if (m_step != Step::QuickConnect) return;
    ++m_attempt;
    m_poll.stop();
    m_quickConnectCode.clear();
    Q_EMIT quickConnectCodeChanged();
    setStep(Step::Credentials);
  }

  void SignIn::finishSignIn(const api::AuthenticationResult& result) {
    if (!result.serverId.isEmpty()) m_server.id = result.serverId;
    m_server.userId = result.user.id;
    m_server.userName = result.user.name;
    m_server.lastUsed = QDateTime::currentDateTimeUtc();
    m_servers.save(m_server);
    m_tokenSaved = m_tokens.write(platform::accessTokenKey(m_server.id, m_server.userId),
                                  result.accessToken) == platform::CredentialStore::Status::Ok;
    m_userName = result.user.name;
    m_quickConnectCode.clear();
    Q_EMIT quickConnectCodeChanged();
    Q_EMIT savedChanged();
    Q_EMIT serverChanged();
    setStep(Step::SignedIn);
    Q_EMIT signedIn();
  }

  bool SignIn::restore() {
    const auto saved = m_servers.active();
    if (!saved || saved->userId.isEmpty()) return false;
    const auto token = m_tokens.read(platform::accessTokenKey(saved->id, saved->userId));
    if (token.status != platform::CredentialStore::Status::Ok || token.secret.isEmpty())
      return false;
    const quint64 attempt = ++m_attempt;
    m_server = *saved;
    m_userName = saved->userName;
    m_tokenSaved = true;
    m_client.setServerUrl(saved->url);
    m_client.setSession(token.secret, saved->userId);
    Q_EMIT serverChanged();
    setError({});
    setStep(Step::SignedIn);
    Q_EMIT signedIn();
    // Checked after the fact: a rejected token brings the login back through
    // sessionExpired.
    m_client.currentUser(this, [this, attempt](api::Result<api::UserInfo> user) {
      if (attempt != m_attempt || !user) return;
      if (user->name != m_userName) {
        m_userName = user->name;
        m_server.userName = user->name;
        m_servers.save(m_server);
        Q_EMIT stepChanged();
      }
    });
    m_servers.setActive(saved->id);
    return true;
  }

  void SignIn::signOut() {
    if (m_step != Step::SignedIn) return;
    ++m_attempt;
    const SavedServer server = m_server;
    m_tokens.remove(platform::accessTokenKey(server.id, server.userId));
    m_servers.clearUser(server.id);
    m_client.signOut(this, [](api::Result<api::Empty>) {});
    m_server.userId.clear();
    Q_EMIT savedChanged();
    setStep(Step::Credentials);
    Q_EMIT signedOut();
  }

  void SignIn::onSessionExpired() {
    if (m_step != Step::SignedIn) return;
    ++m_attempt;
    m_tokens.remove(platform::accessTokenKey(m_server.id, m_server.userId));
    m_servers.clearUser(m_server.id);
    m_server.userId.clear();
    Q_EMIT savedChanged();
    setError(tr("The session has ended. Sign in again."));
    // The same server; Quick Connect may have been turned off meanwhile.
    m_client.quickConnectEnabled(this, [this](api::Result<bool> enabled) {
      m_quickConnect = enabled.ok() && enabled.value();
      Q_EMIT serverChanged();
    });
    setStep(Step::Credentials);
    Q_EMIT signedOut();
  }

} // namespace jfd::library
