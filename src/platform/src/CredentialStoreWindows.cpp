#include <jfd/platform/CredentialStore.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// wincred.h needs the types from windows.h.
#include <wincred.h>

namespace jfd::platform {

  namespace {
    // Credentials appear in the Credential Manager under this prefix.
    std::wstring targetName(const QString& key) {
      return (QStringLiteral("JellyfinDesktopNext/") + key).toStdWString();
    }

    class WindowsCredentialStore final : public CredentialStore {
    public:
      bool isAvailable() const override { return true; }

      ReadResult read(const QString& key) const override {
        const std::wstring target = targetName(key);
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
          return {GetLastError() == ERROR_NOT_FOUND ? Status::NotFound : Status::Failed, {}};
        }
        const auto* bytes = reinterpret_cast<const char*>(credential->CredentialBlob);
        QString secret =
            QString::fromUtf8(bytes, static_cast<qsizetype>(credential->CredentialBlobSize));
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
        CredFree(credential);
        return {Status::Ok, secret};
      }

      Status write(const QString& key, const QString& secret) override {
        const std::wstring target = targetName(key);
        QByteArray blob = secret.toUtf8();
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        const BOOL ok = CredWriteW(&credential, 0);
        SecureZeroMemory(blob.data(), static_cast<SIZE_T>(blob.size()));
        return ok ? Status::Ok : Status::Failed;
      }

      Status remove(const QString& key) override {
        const std::wstring target = targetName(key);
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) return Status::Ok;
        return GetLastError() == ERROR_NOT_FOUND ? Status::Ok : Status::Failed;
      }
    };
  } // namespace

  std::unique_ptr<CredentialStore> createSystemCredentialStore() {
    return std::make_unique<WindowsCredentialStore>();
  }

} // namespace jfd::platform
