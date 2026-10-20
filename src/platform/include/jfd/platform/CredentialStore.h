#pragma once

#include <QHash>
#include <QString>

#include <memory>
#include <optional>

// Secrets (access tokens) in the operating system's credential storage:
// Windows Credential Manager, the Secret Service on Linux. Nothing secret is
// written to settings files or logs.
namespace jfd::platform {

  class CredentialStore {
  public:
    enum class Status { Ok, NotFound, Unavailable, Failed };

    struct ReadResult {
      Status status = Status::NotFound;
      QString secret;
    };

    virtual ~CredentialStore() = default;

    // False when the platform has no usable store (for example no Secret
    // Service running); the app then asks the user to sign in each time.
    virtual bool isAvailable() const = 0;
    virtual ReadResult read(const QString& key) const = 0;
    virtual Status write(const QString& key, const QString& secret) = 0;
    // Removing a missing key succeeds.
    virtual Status remove(const QString& key) = 0;
  };

  // The store for this platform. Falls back to an unavailable store when the
  // platform has none.
  std::unique_ptr<CredentialStore> createSystemCredentialStore();

  // A store that keeps nothing and reports Unavailable.
  std::unique_ptr<CredentialStore> createUnavailableCredentialStore();

  // Key for a user's access token on one server.
  QString accessTokenKey(const QString& serverId, const QString& userId);

  // Keeps secrets in memory only; for tests and as a session-only fallback.
  class MemoryCredentialStore final : public CredentialStore {
  public:
    bool isAvailable() const override { return true; }
    ReadResult read(const QString& key) const override;
    Status write(const QString& key, const QString& secret) override;
    Status remove(const QString& key) override;

  private:
    QHash<QString, QString> m_secrets;
  };

} // namespace jfd::platform
