#pragma once

#include <QByteArrayView>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class QIODevice;

// Checks the minisign signature (file.minisig) that comes with every release
// file. The SHA-256 in the release notes only catches broken downloads; the
// signature proves the file was built and signed by the project's CI.
namespace jfd::integrations {

  struct MinisignPublicKey {
    std::array<std::uint8_t, 8> keyId{};
    std::array<std::uint8_t, 32> key{};
  };

  // A minisign public key: the base64 line ("RW..."), or the whole .pub file
  // with its "untrusted comment:" line.
  std::optional<MinisignPublicKey> parsePublicKey(QByteArrayView text);

  // The keys a release file may be signed with. More than one only during a
  // key rotation.
  using MinisignKeys = std::vector<MinisignPublicKey>;

  // Key lines ("RW...") separated by white space, commas or semicolons; the
  // same key twice counts once. Empty when any entry is not a key, so a
  // mistyped list trusts nothing rather than part of it.
  MinisignKeys parsePublicKeys(QByteArrayView text);

  // The keys release files are signed with, built into the client: the
  // current one (JFD_RELEASE_PUBLIC_KEY) and, during a key rotation, the next
  // one (JFD_RELEASE_NEXT_PUBLIC_KEY). Empty in builds made without them: such
  // a client installs no update.
  const MinisignKeys& releasePublicKeys();

  struct SignatureCheck {
    bool valid = false;
    QString error;          // Why not, when not valid.
    QString trustedComment; // Signed together with the file.
  };

  // Reads data to the end. Only prehashed signatures (BLAKE2b-512, minisign's
  // default "ED") are accepted; both the file's and the trusted comment's
  // signatures must hold.
  SignatureCheck verifySignature(QIODevice& data, QByteArrayView signatureFile, const MinisignPublicKey& key);
  // The same with the key the signature names (its key id) out of keys.
  SignatureCheck verifySignature(QIODevice& data, QByteArrayView signatureFile,
                                 std::span<const MinisignPublicKey> keys);

  // verifySignature, and the trusted comment must name the file
  // ("file:<fileName>"), so a validly signed file can't stand in for another.
  SignatureCheck verifyReleaseFile(QIODevice& data, QByteArrayView signatureFile, const QString& fileName,
                                   const MinisignPublicKey& key);
  SignatureCheck verifyReleaseFile(QIODevice& data, QByteArrayView signatureFile, const QString& fileName,
                                   std::span<const MinisignPublicKey> keys);

} // namespace jfd::integrations
