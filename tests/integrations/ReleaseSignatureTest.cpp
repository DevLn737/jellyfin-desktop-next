#include "../support/DoctestQt.h"

#include <jfd/integrations/ReleaseSignature.h>

#include <QBuffer>

using namespace jfd::integrations;

// Signatures made by minisign 0.11 with throwaway test keys.
namespace {
  constexpr const char* testKey = "untrusted comment: minisign public key B967CF83996D37C\n"
                                  "RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ/eKlWhWeCzjBeQ\n";
  constexpr const char* otherKey = "RWT6qEN104/N921aZWivUpJfSxqO+al9SZ8cuLv54tRBa2fF15QjGGHP";

  constexpr const char* msiName = "JellyfinDesktop-0.2.0.msi";
  const QByteArray msi = QByteArrayLiteral("Jellyfin Desktop release file\n");
  constexpr const char* msiSignature =
      "untrusted comment: signature from minisign secret key\n"
      "RUR805Y5+HyWC8HFFd43qs2oAYPq5MzIg3FTgPAoUfwsKG7GqRslmSMiSLxNbao7P0CXqUWkWyiN7zmAmCEaxbRItQeUAMrozQ0=\n"
      "trusted comment: file:JellyfinDesktop-0.2.0.msi\n"
      "HinFq9aWl91Tg81RplyMK7EwicC2UCUj1dtWgO+3e6J3z4Ss8ODDFpRKz/6R43g3jmBZwT86nFEKZbKYHVOfDw==\n";
  // minisign -l: the file itself is signed, not its hash.
  constexpr const char* msiLegacySignature =
      "untrusted comment: signature from minisign secret key\n"
      "RWR805Y5+HyWCyPmq4oZcm/Yr09L1LqzAz8VvOivd4q2Wmh7YN3qf5QU9tRC5sm84ROBqCtxOHQgUFE4KOivwo583NdeLh0Hcgs=\n"
      "trusted comment: file:JellyfinDesktop-0.2.0.msi\n"
      "6U+7lovkHz2AVwZzV/ainYUpz3ux6VTH+6Z5kyDox2T4vJS2AyyKGhDY8NhGyVFBfbBO2zyrMzWqQWQ/1BGbBg==\n";
  constexpr const char* msiOtherKeySignature =
      "untrusted comment: signature from minisign secret key\n"
      "RUT6qEN104/N9xsLpbsXl/8q8hCrau+3fEMc86/ORdgIEaO4PyUanVfGZcem/AdZ1tXCO7IdVEmCfhbqfrZ5b1xkEtUBAq9yiw0=\n"
      "trusted comment: file:JellyfinDesktop-0.2.0.msi\n"
      "0eKgedRbI/uwc5t1eXDEsQ9tmrzWQPomw+tY36bMumyWn0sE4Ibpp7FhzINV9q1e/8a21pnT0OD7etj6dXV3AQ==\n";

  // 300 000 bytes, read in several chunks; minisign's default comment.
  constexpr const char* appImageName = "JellyfinDesktop-0.2.0-x86_64.AppImage";
  constexpr const char* appImageSignature =
      "untrusted comment: signature from minisign secret key\r\n"
      "RUR805Y5+HyWCxXeM9nDV8usZRNUBYwF+Bvag25SRrT7Y+QkgLjAddTY2XUxYa+dhLkDNNB0FAVyEZzABcLsLX+cWN0hTb3G6Q0=\r\n"
      "trusted comment: timestamp:1761370000\tfile:JellyfinDesktop-0.2.0-x86_64.AppImage\thashed\r\n"
      "4alu3TY/bshY8Le/aaoSZ51dMF3O13yYJ96NF76vRidednDBaXUEEGX65JL+2e/G0+IpBP2EGHa9PMgVhRbnAQ==\r\n";
  QByteArray appImage() {
    QByteArray data(300000, Qt::Uninitialized);
    for (qsizetype i = 0; i < data.size(); ++i) data[i] = static_cast<char>((i * 7) % 251);
    return data;
  }

  SignatureCheck check(QByteArray data, const char* signature, const char* name, const char* key = testKey) {
    QBuffer buffer(&data);
    REQUIRE(buffer.open(QIODevice::ReadOnly));
    const auto publicKey = parsePublicKey(key);
    REQUIRE(publicKey);
    return verifyReleaseFile(buffer, signature, QString::fromLatin1(name), *publicKey);
  }
} // namespace

TEST_CASE("Public keys parse from the .pub file or its key line") {
  const auto fromFile = parsePublicKey(testKey);
  const auto fromLine = parsePublicKey("RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ/eKlWhWeCzjBeQ");
  REQUIRE(fromFile);
  REQUIRE(fromLine);
  CHECK(fromFile->key == fromLine->key);
  CHECK(fromFile->keyId == fromLine->keyId);
  CHECK_FALSE(parsePublicKey(""));
  CHECK_FALSE(parsePublicKey("RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ"));
  CHECK_FALSE(parsePublicKey("not base64 at all!"));
}

TEST_CASE("A signed release file is accepted") {
  const SignatureCheck msiCheck = check(msi, msiSignature, msiName);
  CHECK_MESSAGE(msiCheck.valid, msiCheck.error.toStdString());
  CHECK(msiCheck.trustedComment == QStringLiteral("file:JellyfinDesktop-0.2.0.msi"));

  const SignatureCheck appImageCheck = check(appImage(), appImageSignature, appImageName);
  CHECK_MESSAGE(appImageCheck.valid, appImageCheck.error.toStdString());
}

TEST_CASE("A tampered file is rejected") {
  QByteArray tampered = msi;
  tampered[3] = 'X';
  CHECK_FALSE(check(tampered, msiSignature, msiName).valid);
  CHECK_FALSE(check(msi + "\n", msiSignature, msiName).valid);
  QByteArray tamperedImage = appImage();
  tamperedImage[250000] = static_cast<char>(tamperedImage[250000] ^ 1);
  CHECK_FALSE(check(tamperedImage, appImageSignature, appImageName).valid);
}

TEST_CASE("A tampered trusted comment is rejected") {
  QByteArray signature(msiSignature);
  signature.replace("file:JellyfinDesktop-0.2.0.msi", "file:JellyfinDesktop-0.3.0.msi");
  const SignatureCheck result = check(msi, signature.constData(), "JellyfinDesktop-0.3.0.msi");
  CHECK_FALSE(result.valid);
  CHECK(result.error.contains(QStringLiteral("trusted comment")));
}

TEST_CASE("A signature for another file name is rejected") {
  const SignatureCheck result = check(msi, msiSignature, "JellyfinDesktop-0.3.0.msi");
  CHECK_FALSE(result.valid);
  CHECK(result.error == QStringLiteral("the signature is for another file"));
}

TEST_CASE("A signature from another key, a legacy one or a broken file is rejected") {
  CHECK(check(msi, msiOtherKeySignature, msiName).error == QStringLiteral("signed with another key"));
  CHECK(check(msi, msiSignature, msiName, otherKey).error == QStringLiteral("signed with another key"));
  CHECK_FALSE(check(msi, msiLegacySignature, msiName).valid);
  CHECK_FALSE(check(msi, "", msiName).valid);
  CHECK_FALSE(check(msi, "untrusted comment: x\nRUR8\ntrusted comment: x\nHinF\n", msiName).valid);
}

// Key rotation: a client built with the current and the next key accepts
// releases signed with either, and picks the key by the signature's key id.
TEST_CASE("A key list accepts a signature from any of its keys") {
  const MinisignKeys keys = parsePublicKeys(QByteArray("RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ/eKlWhWeCzjBeQ ") +
                                            otherKey);
  REQUIRE(keys.size() == 2);
  for (const char* signature : {msiSignature, msiOtherKeySignature}) {
    QByteArray data = msi;
    QBuffer buffer(&data);
    REQUIRE(buffer.open(QIODevice::ReadOnly));
    const SignatureCheck result = verifyReleaseFile(buffer, signature, QString::fromLatin1(msiName), keys);
    CHECK_MESSAGE(result.valid, result.error.toStdString());
  }

  // Only the current key: a release signed with the next one is refused.
  const MinisignKeys current = parsePublicKeys("RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ/eKlWhWeCzjBeQ");
  REQUIRE(current.size() == 1);
  QByteArray data = msi;
  QBuffer buffer(&data);
  REQUIRE(buffer.open(QIODevice::ReadOnly));
  CHECK(verifyReleaseFile(buffer, msiOtherKeySignature, QString::fromLatin1(msiName), current).error ==
        QStringLiteral("signed with another key"));

  // A tampered file still fails with the right key picked from the list.
  QByteArray tampered = msi;
  tampered[3] = 'X';
  QBuffer tamperedBuffer(&tampered);
  REQUIRE(tamperedBuffer.open(QIODevice::ReadOnly));
  CHECK_FALSE(verifyReleaseFile(tamperedBuffer, msiOtherKeySignature, QString::fromLatin1(msiName), keys).valid);

  // No keys: nothing is trusted.
  QByteArray none = msi;
  QBuffer noneBuffer(&none);
  REQUIRE(noneBuffer.open(QIODevice::ReadOnly));
  CHECK_FALSE(verifyReleaseFile(noneBuffer, msiSignature, QString::fromLatin1(msiName), MinisignKeys{}).valid);
}

TEST_CASE("Key lists parse separators, drop duplicates and refuse a bad entry") {
  const QByteArray current("RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ/eKlWhWeCzjBeQ");
  CHECK(parsePublicKeys(current + "," + otherKey).size() == 2);
  CHECK(parsePublicKeys(current + " ; " + otherKey + "\n").size() == 2);
  CHECK(parsePublicKeys(current + " " + current).size() == 1);
  CHECK(parsePublicKeys(" ").empty());
  CHECK(parsePublicKeys("").empty());
  CHECK(parsePublicKeys(current + " RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ").empty());
}
