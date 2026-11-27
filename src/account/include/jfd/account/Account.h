#pragma once

#include <jfd/account/ServerStore.h>
#include <jfd/account/SignIn.h>
#include <jfd/account/UserScope.h>
#include <jfd/api/ClientIdentity.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/platform/AsyncCredentialStore.h>
#include <jfd/platform/CredentialStore.h>

#include <QObject>
#include <QString>

#include <chrono>
#include <memory>

namespace jfd::account {

  // The connection to one server as one user, shared by every module: the
  // client and its session socket, the saved servers and tokens, the way in
  // (SignIn) and the user scope. On sign-in the socket starts, then the
  // scope begins and sessionBegan follows; on sign-out (or when the server
  // ends the session) the scope ends, the socket stops and sessionEnded
  // follows. The screens move between login and home on those two signals.
  //
  // Goes after everything that holds the client by reference.
  class Account : public QObject {
    Q_OBJECT
  public:
    struct Config {
      api::ClientIdentity identity;
      // INI file of the saved servers.
      QString serversFile;
      // Null: tokens are not kept (the next start asks for the password).
      std::unique_ptr<platform::CredentialStore> tokens;
    };

    explicit Account(Config config, QObject* parent = nullptr);
    ~Account() override;

    api::JellyfinClient& client() { return m_client; }
    api::SessionSocket& socket() { return m_socket; }
    SignIn& signIn() { return m_signIn; }
    ServerStore& servers() { return m_servers; }
    UserScope& scope() { return m_scope; }

    bool signedIn() const { return m_scope.active(); }

    // Signs in with the saved server and token, if any (SignIn::restore).
    // The token is read off this thread; wait bounds how long this call
    // waits for it, so a store that answers at once (the usual case) signs
    // in before the window shows, and a slow one (a locked keyring asking
    // for its password) lets the window show with "Signing in" meanwhile.
    bool restore(std::chrono::milliseconds wait = std::chrono::milliseconds(0));

    platform::AsyncCredentialStore& tokens() { return m_tokens; }

  Q_SIGNALS:
    // After the scope began: every module has the new user.
    void sessionBegan();
    // After the scope ended and the socket stopped.
    void sessionEnded();

  private:
    void begin();
    void end();

    // First: the worker finishes its writes after everything else is gone.
    platform::AsyncCredentialStore m_tokens;
    ServerStore m_servers;
    api::JellyfinClient m_client;
    api::SessionSocket m_socket;
    SignIn m_signIn;
    UserScope m_scope;
  };

} // namespace jfd::account
