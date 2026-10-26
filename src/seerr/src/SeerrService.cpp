#include <jfd/seerr/SeerrService.h>

namespace jfd::seerr {

  namespace {
    // Seerr's permission bits (docs/seerr.md): admin, request.
    constexpr int AdminBit = 2;
    constexpr int RequestBit = 32;

    bool canRequest(const User& user) {
      return (user.permissions & (AdminBit | RequestBit)) != 0;
    }
  } // namespace

  Outcome outcomeOf(Refusal refusal) {
    switch (refusal) {
    case Refusal::SignedOut: return Outcome::SignedOut;
    case Refusal::BadLogin: return Outcome::BadLogin;
    case Refusal::NoPermission: return Outcome::NoPermission;
    case Refusal::AlreadyDone: return Outcome::AlreadyDone;
    case Refusal::Unreachable: return Outcome::Unreachable;
    case Refusal::ServerError: return Outcome::ServerError;
    case Refusal::NotFound:
    case Refusal::Other: break;
    }
    return Outcome::Other;
  }

  SeerrService::SeerrService(platform::CredentialStore& store, QObject* parent)
      : QObject(parent), m_sessions(store) {
    connect(&m_client, &SeerrClient::sessionExpired, this, [this] {
      forgetSession(true);
      Q_EMIT stateChanged();
    });
  }

  void SeerrService::setAddress(const QString& text) {
    const auto url = SeerrClient::normalizeServerUrl(text);
    const QUrl next = url.value_or(QUrl());
    if (next == m_client.serverUrl()) return;
    forgetSession(false);
    m_client.setServerUrl(next);
    if (next.isValid()) {
      const SessionStore::Saved saved = m_sessions.load(next);
      if (!saved.cookie.isEmpty()) {
        m_client.setSession(saved.cookie, saved.userId);
        // The name and permissions come from the server; until then the
        // session counts as signed in.
        m_client.currentUser(this, [this](api::Result<User> user) {
          if (user.ok()) {
            adopt(*user);
            Q_EMIT stateChanged();
          }
          // A refused session was already dropped by the client.
        });
      }
    }
    Q_EMIT stateChanged();
  }

  QString SeerrService::serverHost() const {
    const QUrl url = m_client.serverUrl();
    if (!url.isValid()) return {};
    return url.port() > 0 ? QStringLiteral("%1:%2").arg(url.host()).arg(url.port()) : url.host();
  }

  bool SeerrService::cleartextOverInternet() const {
    return configured() && SeerrClient::isCleartextOverInternet(m_client.serverUrl());
  }

  void SeerrService::adopt(const User& user) {
    m_userName = user.name;
    m_mayRequest = canRequest(user);
  }

  void SeerrService::forgetSession(bool removeSaved) {
    if (removeSaved && configured()) m_sessions.clear(m_client.serverUrl());
    m_client.clearSession();
    m_userName.clear();
    m_mayRequest = false;
  }

  void SeerrService::signIn(const QString& userName, const QString& password) {
    if (!configured()) {
      Q_EMIT signInFinished(int(Outcome::NoAddress));
      return;
    }
    if (m_busy) return;
    m_busy = true;
    // The password goes only to a server that says it is a Seerr on Jellyfin.
    m_client.probe(this, [this, userName, password](api::Result<ServerInfo> info) {
      if (!info.ok()) {
        m_busy = false;
        Q_EMIT signInFinished(int(outcomeOf(refusalOf(info.error()))));
        return;
      }
      if (!info->usesJellyfin()) {
        m_busy = false;
        Q_EMIT signInFinished(int(Outcome::NotJellyfin));
        return;
      }
      m_client.signIn(userName, password, this, [this](api::Result<User> user) {
        m_busy = false;
        if (!user.ok()) {
          Q_EMIT signInFinished(int(outcomeOf(refusalOf(user.error(), true))));
          return;
        }
        adopt(*user);
        // Without a credential store the session lives for this run.
        m_sessions.save(m_client.serverUrl(), m_client.sessionCookie(), m_client.userId());
        Q_EMIT stateChanged();
        Q_EMIT signInFinished(int(Outcome::Ok));
      });
    });
  }

  void SeerrService::check() {
    if (!configured()) {
      Q_EMIT checkFinished(int(Outcome::NoAddress), {});
      return;
    }
    m_client.probe(this, [this](api::Result<ServerInfo> info) {
      if (!info.ok()) {
        Q_EMIT checkFinished(int(outcomeOf(refusalOf(info.error()))), {});
        return;
      }
      if (!info->usesJellyfin()) {
        Q_EMIT checkFinished(int(Outcome::NotJellyfin), {});
        return;
      }
      if (!m_client.isSignedIn()) {
        Q_EMIT checkFinished(int(Outcome::SignedOut), {});
        return;
      }
      m_client.currentUser(this, [this](api::Result<User> user) {
        if (!user.ok()) {
          Q_EMIT checkFinished(int(outcomeOf(refusalOf(user.error()))), {});
          return;
        }
        adopt(*user);
        Q_EMIT stateChanged();
        Q_EMIT checkFinished(int(Outcome::Ok), user->name);
      });
    });
  }

  void SeerrService::signOut() {
    if (!configured()) return;
    if (m_client.isSignedIn()) m_client.signOut(this, [](api::Result<api::Empty>) {});
    forgetSession(true);
    Q_EMIT stateChanged();
  }

  QUrl SeerrService::imageUrl(const QString& path, int width) {
    if (path.isEmpty()) return {};
    return QUrl(QStringLiteral("https://image.tmdb.org/t/p/w%1%2").arg(width).arg(path));
  }

} // namespace jfd::seerr
