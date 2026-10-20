#include <jfd/platform/CredentialStore.h>

// Platforms without a supported store yet (macOS Keychain comes later, and
// Linux builds without libsecret).
namespace jfd::platform {

  std::unique_ptr<CredentialStore> createSystemCredentialStore() {
    return createUnavailableCredentialStore();
  }

} // namespace jfd::platform
