#include "../support/DoctestQt.h"

#include <jfd/platform/CredentialStore.h>

#include <QUuid>

using namespace jfd::platform;
using Status = CredentialStore::Status;

TEST_CASE("The memory store keeps, replaces and removes secrets") {
  MemoryCredentialStore store;
  const QString key = accessTokenKey(QStringLiteral("server1"), QStringLiteral("user1"));
  CHECK(key == QStringLiteral("server/server1/user/user1/token"));
  CHECK(store.read(key).status == Status::NotFound);
  CHECK(store.write(key, QStringLiteral("a")) == Status::Ok);
  CHECK(store.write(key, QStringLiteral("b")) == Status::Ok);
  CHECK(store.read(key).secret == QStringLiteral("b"));
  CHECK(store.remove(key) == Status::Ok);
  CHECK(store.remove(key) == Status::Ok);
  CHECK(store.read(key).status == Status::NotFound);
}

TEST_CASE("The unavailable store refuses everything") {
  auto store = createUnavailableCredentialStore();
  CHECK_FALSE(store->isAvailable());
  CHECK(store->read(QStringLiteral("k")).status == Status::Unavailable);
  CHECK(store->write(QStringLiteral("k"), QStringLiteral("v")) == Status::Unavailable);
}

// Runs against the real Credential Manager or Secret Service when there is
// one (Windows CI always has it; Linux CI has no Secret Service daemon).
TEST_CASE("The system store round-trips a secret when available") {
  auto store = createSystemCredentialStore();
  if (!store->isAvailable()) {
    MESSAGE("No system credential store on this machine");
    return;
  }
  const QString key = QStringLiteral("test/") + QUuid::createUuid().toString(QUuid::Id128);
  const QString secret = QStringLiteral("token-Привет-%1").arg(42);
  REQUIRE(store->write(key, secret) == Status::Ok);
  const auto read = store->read(key);
  CHECK(read.status == Status::Ok);
  CHECK(read.secret == secret);
  CHECK(store->remove(key) == Status::Ok);
  CHECK(store->read(key).status == Status::NotFound);
  CHECK(store->remove(key) == Status::Ok);
}
