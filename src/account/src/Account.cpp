#include <jfd/account/Account.h>

namespace jfd::account {

  Account::Account(Config config, QObject* parent)
      : QObject(parent),
        m_tokens(config.tokens ? std::move(config.tokens) : platform::createUnavailableCredentialStore()),
        m_servers(config.serversFile), m_client(config.identity), m_socket(m_client),
        m_signIn(m_client, m_servers, *m_tokens) {
    connect(&m_signIn, &SignIn::signedIn, this, &Account::begin);
    connect(&m_signIn, &SignIn::signedOut, this, &Account::end);
  }

  Account::~Account() {
    m_socket.stop();
  }

  void Account::begin() {
    // A second sign-in without a sign-out between (the token was replaced
    // in place): the previous user's state goes first.
    if (m_scope.active()) end();
    m_socket.start();
    m_scope.begin();
    Q_EMIT sessionBegan();
  }

  void Account::end() {
    m_scope.end();
    m_socket.stop();
    Q_EMIT sessionEnded();
  }

} // namespace jfd::account
