#pragma once

#include <QByteArrayView>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>

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

  // The key release files are signed with, built into the client
  // (JFD_RELEASE_PUBLIC_KEY). None in builds made without it: such a client
  // installs no update.
  std::optional<MinisignPublicKey> releasePublicKey();

  struct SignatureCheck {
    bool valid = false;
    QString error;          // Why not, when not valid.
    QString trustedComment; // Signed together with the file.
  };

  // Reads data to the end. Only prehashed signatures (BLAKE2b-512, minisign's
  // default "ED") are accepted; both the file's and the trusted comment's
  // signatures must hold.
  SignatureCheck verifySignature(QIODevice& data, QByteArrayView signatureFile, const MinisignPublicKey& key);

  // verifySignature, and the trusted comment must name the file
  // ("file:<fileName>"), so a validly signed file can't stand in for another.
  SignatureCheck verifyReleaseFile(QIODevice& data, QByteArrayView signatureFile, const QString& fileName,
                                   const MinisignPublicKey& key);

} // namespace jfd::integrations
