#include <jfd/seerr/SessionStore.h>

#include <jfd/platform/CredentialStore.h>

namespace jfd::seerr {

  QString SessionStore::key(const QUrl& server) {
    return QStringLiteral("seerr-session:%1://%2:%3%4")
        .arg(server.scheme(), server.host().toLower())
        .arg(server.port(server.scheme() == QLatin1String("https") ? 443 : 80))
        .arg(server.path());
  }

  bool SessionStore::save(const QUrl& server, const QString& cookie, int userId) {
    if (cookie.isEmpty() || userId <= 0) return false;
    return m_store.write(key(server), QString::number(userId) + QLatin1Char(':') + cookie) ==
           platform::CredentialStore::Status::Ok;
  }

  SessionStore::Saved SessionStore::load(const QUrl& server) const {
    const auto result = m_store.read(key(server));
    if (result.status != platform::CredentialStore::Status::Ok) return {};
    const qsizetype colon = result.secret.indexOf(QLatin1Char(':'));
    if (colon <= 0) return {};
    Saved saved;
    saved.userId = result.secret.left(colon).toInt();
    saved.cookie = result.secret.mid(colon + 1);
    if (saved.userId <= 0 || saved.cookie.isEmpty()) return {};
    return saved;
  }

  void SessionStore::clear(const QUrl& server) {
    m_store.remove(key(server));
  }

} // namespace jfd::seerr
