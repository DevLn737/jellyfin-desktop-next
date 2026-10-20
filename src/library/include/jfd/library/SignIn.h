#pragma once

#include <jfd/api/Models.h>
#include <jfd/library/ServerStore.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

namespace jfd::api {
  class JellyfinClient;
  class ServerDiscovery;
} // namespace jfd::api

namespace jfd::platform {
  class CredentialStore;
}

namespace jfd::library {

  // The way into a server, for the login screen:
  //
  //   ChooseServer  servers found on the network, saved servers, an address
  //   CheckingServer  the server's version and whether Quick Connect is on
  //   Credentials  user name and password, or Quick Connect
  //   SigningIn / QuickConnect  waiting for the server
  //   SignedIn
  //
  // A successful sign-in stores the token in the system credential store and
  // the server in ServerStore; restore() signs in with them at start-up. When
  // the server later rejects the token, the screen goes back to Credentials
  // for the same server.
  class SignIn : public QObject {
    Q_OBJECT
    Q_PROPERTY(Step step READ step NOTIFY stepChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stepChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString serverName READ serverName NOTIFY serverChanged)
    Q_PROPERTY(QString serverAddress READ serverAddress NOTIFY serverChanged)
    Q_PROPERTY(bool quickConnectAvailable READ quickConnectAvailable NOTIFY serverChanged)
    Q_PROPERTY(QString quickConnectCode READ quickConnectCode NOTIFY quickConnectCodeChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY stepChanged)
    // False when the system has no credential store: the next start asks
    // for the password again.
    Q_PROPERTY(bool tokenSaved READ tokenSaved NOTIFY stepChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)
    // [{id, name, address}]
    Q_PROPERTY(QVariantList discoveredServers READ discoveredServers NOTIFY discoveredChanged)
    // [{id, name, address, userName}]
    Q_PROPERTY(QVariantList savedServers READ savedServers NOTIFY savedChanged)

  public:
    enum class Step {
      ChooseServer,
      CheckingServer,
      Credentials,
      SigningIn,
      QuickConnect,
      SignedIn
    };
    Q_ENUM(Step)

    SignIn(api::JellyfinClient& client, ServerStore& servers, platform::CredentialStore& tokens,
           QObject* parent = nullptr);
    ~SignIn() override;

    Step step() const { return m_step; }
    bool busy() const;
    QString errorText() const { return m_errorText; }
    QString serverName() const { return m_server.name; }
    QString serverAddress() const { return m_server.url.toString(); }
    bool quickConnectAvailable() const { return m_quickConnect; }
    QString quickConnectCode() const { return m_quickConnectCode; }
    QString userName() const { return m_userName; }
    bool tokenSaved() const { return m_tokenSaved; }
    bool discovering() const;
    QVariantList discoveredServers() const;
    QVariantList savedServers() const;

    void setQuickConnectPollMs(int ms) { m_poll.setInterval(ms); }
    api::ServerDiscovery& discovery() { return *m_discovery; }

    // Signs in with the active saved server and its stored token. Returns
    // false (and stays on ChooseServer) when there is nothing to restore. A
    // server that cannot be reached still counts as signed in: the screens
    // show the error and retry, as they would after a dropped connection.
    Q_INVOKABLE bool restore();
    Q_INVOKABLE void discover();
    // A typed address or one from the lists.
    Q_INVOKABLE void connectToServer(const QString& address);
    Q_INVOKABLE void chooseSavedServer(const QString& serverId);
    Q_INVOKABLE void forgetServer(const QString& serverId);
    Q_INVOKABLE void signIn(const QString& userName, const QString& password);
    Q_INVOKABLE void startQuickConnect();
    Q_INVOKABLE void cancelQuickConnect();
    // From Credentials back to the server choice.
    Q_INVOKABLE void back();
    // Revokes the token on the server and here; the server stays in the list.
    Q_INVOKABLE void signOut();

  Q_SIGNALS:
    void stepChanged();
    void errorTextChanged();
    void serverChanged();
    void quickConnectCodeChanged();
    void discoveringChanged();
    void discoveredChanged();
    void savedChanged();
    void signedIn();
    void signedOut();

  private:
    void setStep(Step step);
    void setError(const QString& text);
    void checkServer(const QUrl& url, const QString& preferredUserName);
    void finishSignIn(const api::AuthenticationResult& result);
    void pollQuickConnect();
    void onSessionExpired();
    QString describe(const api::ApiError& error) const;

    api::JellyfinClient& m_client;
    ServerStore& m_servers;
    platform::CredentialStore& m_tokens;
    api::ServerDiscovery* m_discovery = nullptr;
    Step m_step = Step::ChooseServer;
    QString m_errorText;
    SavedServer m_server;
    bool m_quickConnect = false;
    QString m_quickConnectSecret;
    QString m_quickConnectCode;
    QTimer m_poll;
    QString m_userName;
    bool m_tokenSaved = false;
    quint64 m_attempt = 0;
  };

} // namespace jfd::library
