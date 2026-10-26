#pragma once

#include <jfd/seerr/SeerrClient.h>
#include <jfd/seerr/SessionStore.h>

#include <QObject>
#include <QString>
#include <QUrl>

namespace jfd::seerr {

  // What a sign-in, a connection check or a request came to. The screens
  // turn it into text; the numbers are what QML sees.
  enum class Outcome {
    Ok = 0,
    // Not configured or no address that can be used.
    NoAddress,
    Unreachable,
    // The server is not a Seerr set up with Jellyfin as its media server.
    NotJellyfin,
    // The session is gone: sign in again.
    SignedOut,
    BadLogin,
    NoPermission,
    AlreadyDone,
    ServerError,
    Other,
  };
  Outcome outcomeOf(Refusal refusal);

  // The user's Seerr connection: one address (a user setting, never part of
  // the program), the sign-in as their Jellyfin account and the session
  // that sign-in gives. Without an address everything about Seerr stays off.
  //
  // The password is held only while signIn() runs and is never stored; the
  // session is the cookie Seerr sets, kept in the credential storage under
  // the address it belongs to. A change of address drops the session in
  // memory (the saved one for the old address stays for when it comes back).
  class SeerrService : public QObject {
    Q_OBJECT

  public:
    // `store` outlives the service. A store that is not available keeps the
    // session for this run only.
    SeerrService(platform::CredentialStore& store, QObject* parent = nullptr);

    SeerrClient& client() { return m_client; }

    // The address as the user typed it; empty or not a usable address
    // switches Seerr off. Restores the saved session for it.
    void setAddress(const QString& text);
    bool configured() const { return m_client.serverUrl().isValid(); }
    QUrl serverUrl() const { return m_client.serverUrl(); }
    // "host:port" for the confirmation texts.
    QString serverHost() const;
    // A plain http address that is not on the local network.
    bool cleartextOverInternet() const;

    bool signedIn() const { return configured() && m_client.isSignedIn(); }
    QString userName() const { return m_userName; }
    // Whether the account may make requests (Seerr's admin or request
    // permission); the server has the last word.
    bool mayRequest() const { return m_mayRequest; }
    int userId() const { return m_client.userId(); }

    // Asks the server who it is before the password is sent, then signs in.
    void signIn(const QString& userName, const QString& password);
    // Checks the address and, when signed in, the session.
    void check();
    // Ends the session on the server (best effort) and forgets it here.
    void signOut();

    // Seerr's poster URL for a TMDB image path at the card width; empty for
    // no path.
    static QUrl imageUrl(const QString& path, int width = 342);

  Q_SIGNALS:
    void stateChanged();
    void signInFinished(int outcome);
    void checkFinished(int outcome, const QString& userName);

  private:
    void adopt(const User& user);
    void forgetSession(bool removeSaved);

    SeerrClient m_client;
    SessionStore m_sessions;
    QString m_userName;
    bool m_mayRequest = false;
    bool m_busy = false;
  };

} // namespace jfd::seerr
