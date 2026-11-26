#include <jfd/integrations/ReleaseSignature.h>

#include "ReleaseKey.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QIODevice>
#include <QRegularExpression>

#include <algorithm>
#include <cstring>

#include <monocypher-ed25519.h>
#include <monocypher.h>

namespace jfd::integrations {

  namespace {
    constexpr std::size_t hashSize = 64;
    constexpr std::size_t signatureSize = 64;

    std::optional<QByteArray> decode(QByteArrayView line, qsizetype size) {
      auto result = QByteArray::fromBase64Encoding(line.trimmed().toByteArray(),
                                                   QByteArray::AbortOnBase64DecodingErrors);
      if (!result || result.decoded.size() != size) return std::nullopt;
      return std::move(result.decoded);
    }

    QByteArrayList lines(QByteArrayView text) {
      QByteArrayList result;
      for (QByteArray line : text.toByteArray().split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty()) result.append(line);
      }
      return result;
    }

    const auto* bytes(const QByteArray& data) { return reinterpret_cast<const std::uint8_t*>(data.constData()); }

    SignatureCheck failed(const char* reason) { return {false, QString::fromLatin1(reason), {}}; }
  } // namespace

  std::optional<MinisignPublicKey> parsePublicKey(QByteArrayView text) {
    QByteArrayList keyLines = lines(text);
    if (!keyLines.isEmpty() && keyLines.first().startsWith("untrusted comment:")) keyLines.removeFirst();
    if (keyLines.size() != 1) return std::nullopt;
    const auto decoded = decode(keyLines.first(), 42);
    if (!decoded || !decoded->startsWith("Ed")) return std::nullopt;
    MinisignPublicKey key;
    std::memcpy(key.keyId.data(), decoded->constData() + 2, key.keyId.size());
    std::memcpy(key.key.data(), decoded->constData() + 10, key.key.size());
    return key;
  }

  MinisignKeys parsePublicKeys(QByteArrayView text) {
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    MinisignKeys keys;
    for (const QString& entry : QString::fromLatin1(text).split(separators, Qt::SkipEmptyParts)) {
      const auto key = parsePublicKey(entry.toLatin1());
      if (!key) return {};
      const bool known = std::any_of(keys.begin(), keys.end(), [&key](const MinisignPublicKey& other) {
        return other.keyId == key->keyId && other.key == key->key;
      });
      if (!known) keys.push_back(*key);
    }
    return keys;
  }

  const MinisignKeys& releasePublicKeys() {
    static const MinisignKeys keys =
        parsePublicKeys(QByteArrayView(JFD_RELEASE_PUBLIC_KEY " " JFD_RELEASE_NEXT_PUBLIC_KEY));
    return keys;
  }

  SignatureCheck verifySignature(QIODevice& data, QByteArrayView signatureFile, const MinisignPublicKey& key) {
    return verifySignature(data, signatureFile, std::span<const MinisignPublicKey>(&key, 1));
  }

  SignatureCheck verifySignature(QIODevice& data, QByteArrayView signatureFile,
                                 std::span<const MinisignPublicKey> keys) {
    const QByteArrayList sigLines = lines(signatureFile);
    static constexpr QByteArrayView trustedPrefix("trusted comment: ");
    if (sigLines.size() != 4 || !sigLines[0].startsWith("untrusted comment:") ||
        !sigLines[2].startsWith(trustedPrefix))
      return failed("malformed signature file");
    const auto signature = decode(sigLines[1], 2 + 8 + signatureSize);
    const auto globalSignature = decode(sigLines[3], signatureSize);
    if (!signature || !globalSignature) return failed("malformed signature file");
    if (signature->startsWith("Ed")) return failed("legacy signature without prehashing");
    if (!signature->startsWith("ED")) return failed("unknown signature algorithm");
    const auto match = std::find_if(keys.begin(), keys.end(), [&signature](const MinisignPublicKey& candidate) {
      return std::memcmp(signature->constData() + 2, candidate.keyId.data(), candidate.keyId.size()) == 0;
    });
    if (match == keys.end()) return failed("signed with another key");
    const MinisignPublicKey& key = *match;

    crypto_blake2b_ctx context;
    crypto_blake2b_init(&context, hashSize);
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    for (;;) {
      const qint64 read = data.read(chunk.data(), chunk.size());
      if (read < 0) return failed("cannot read the file");
      if (read == 0) break;
      crypto_blake2b_update(&context, bytes(chunk), static_cast<std::size_t>(read));
    }
    std::array<std::uint8_t, hashSize> hash{};
    crypto_blake2b_final(&context, hash.data());

    const auto* fileSignature = bytes(*signature) + 10;
    if (crypto_ed25519_check(fileSignature, key.key.data(), hash.data(), hash.size()) != 0)
      return failed("the file does not match its signature");

    const QByteArray comment = sigLines[2].mid(trustedPrefix.size());
    QByteArray signedComment = signature->mid(10) + comment;
    if (crypto_ed25519_check(bytes(*globalSignature), key.key.data(), bytes(signedComment),
                             static_cast<std::size_t>(signedComment.size())) != 0)
      return failed("the trusted comment does not match its signature");
    return {true, {}, QString::fromUtf8(comment)};
  }

  SignatureCheck verifyReleaseFile(QIODevice& data, QByteArrayView signatureFile, const QString& fileName,
                                   const MinisignPublicKey& key) {
    return verifyReleaseFile(data, signatureFile, fileName, std::span<const MinisignPublicKey>(&key, 1));
  }

  SignatureCheck verifyReleaseFile(QIODevice& data, QByteArrayView signatureFile, const QString& fileName,
                                   std::span<const MinisignPublicKey> keys) {
    SignatureCheck check = verifySignature(data, signatureFile, keys);
    if (!check.valid) return check;
    static const QRegularExpression separators(QStringLiteral("\\s+"));
    if (!check.trustedComment.split(separators).contains(QStringLiteral("file:") + fileName))
      return {false, QStringLiteral("the signature is for another file"), check.trustedComment};
    return check;
  }

} // namespace jfd::integrations
