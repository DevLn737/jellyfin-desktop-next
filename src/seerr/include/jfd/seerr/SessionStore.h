#pragma once

#include <QString>
#include <QUrl>

namespace jfd::platform {
  class CredentialStore;
}

namespace jfd::seerr {

  // The Seerr session cookie in the system credential storage, one per Seerr
  // address. Nothing else about the sign-in is stored: not the password, not
  // the Jellyfin token Seerr returns.
  class SessionStore {
  public:
    explicit SessionStore(platform::CredentialStore& store) : m_store(store) {}

    struct Saved {
      QString cookie;
      int userId = 0;
    };

    static QString key(const QUrl& server);

    bool save(const QUrl& server, const QString& cookie, int userId);
    Saved load(const QUrl& server) const;
    void clear(const QUrl& server);

  private:
    platform::CredentialStore& m_store;
  };

} // namespace jfd::seerr
