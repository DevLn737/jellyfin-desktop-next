#include <jfd/platform/CredentialStore.h>

namespace jfd::platform {

  QString accessTokenKey(const QString& serverId, const QString& userId) {
    return QStringLiteral("server/%1/user/%2/token").arg(serverId, userId);
  }

  CredentialStore::ReadResult MemoryCredentialStore::read(const QString& key) const {
    const auto it = m_secrets.constFind(key);
    if (it == m_secrets.constEnd()) return {Status::NotFound, {}};
    return {Status::Ok, *it};
  }

  CredentialStore::Status MemoryCredentialStore::write(const QString& key, const QString& secret) {
    m_secrets.insert(key, secret);
    return Status::Ok;
  }

  CredentialStore::Status MemoryCredentialStore::remove(const QString& key) {
    m_secrets.remove(key);
    return Status::Ok;
  }

  namespace {
    class UnavailableCredentialStore final : public CredentialStore {
    public:
      bool isAvailable() const override { return false; }
      ReadResult read(const QString&) const override { return {Status::Unavailable, {}}; }
      Status write(const QString&, const QString&) override { return Status::Unavailable; }
      Status remove(const QString&) override { return Status::Unavailable; }
    };
  } // namespace

  std::unique_ptr<CredentialStore> createUnavailableCredentialStore() {
    return std::make_unique<UnavailableCredentialStore>();
  }

} // namespace jfd::platform
