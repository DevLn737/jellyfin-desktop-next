#include "../support/DoctestQt.h"
#include "Wait.h"

#include <jfd/platform/SingleInstance.h>

#include <QDir>
#include <QFile>
#include <QUuid>

#include <atomic>
#include <thread>

using jfd::platform::SingleInstance;
using jfd::test::spinUntil;

namespace {
  QString uniqueKey() { return QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces); }

  // A second start runs on another thread, like another process: its
  // blocking hand-over needs the first instance's event loop to keep running.
  bool startOnAnotherThread(const QString& key, const QStringList& arguments) {
    std::atomic<int> result{-1};
    std::thread second([&] {
      SingleInstance instance(key);
      result = instance.start(arguments) ? 1 : 0;
    });
    spinUntil([&] { return result != -1; }, 10000);
    second.join();
    return result == 1;
  }
} // namespace

TEST_CASE("A second start hands its arguments to the first and exits") {
  const QString key = uniqueKey();
  SingleInstance first(key);
  REQUIRE(first.start({QStringLiteral("app")}));
  CHECK(first.isPrimary());

  QStringList received;
  QObject::connect(&first, &SingleInstance::activated, [&](const QStringList& arguments) { received = arguments; });
  const QStringList arguments{QStringLiteral("app"), QStringLiteral("--fullscreen"), QStringLiteral("файл.mkv")};
  CHECK_FALSE(startOnAnotherThread(key, arguments));
  REQUIRE(spinUntil([&] { return !received.isEmpty(); }));
  CHECK(received == arguments);

  // And again: the first instance keeps listening.
  received.clear();
  CHECK_FALSE(startOnAnotherThread(key, {QStringLiteral("again")}));
  REQUIRE(spinUntil([&] { return !received.isEmpty(); }));
  CHECK(received == QStringList{QStringLiteral("again")});
}

TEST_CASE("Different keys are different instances") {
  SingleInstance first(uniqueKey());
  SingleInstance other(uniqueKey());
  CHECK(first.start({}));
  CHECK(other.start({}));
  CHECK(first.serverName() != other.serverName());
}

#ifndef Q_OS_WIN
TEST_CASE("A socket left by a crashed instance does not block the next start") {
  const QString key = uniqueKey();
  SingleInstance next(key);
  // A socket path nobody listens on, as after a crash (Windows pipes vanish
  // with their process, so this only happens on Unix).
  QFile leftover(QDir::tempPath() + QLatin1Char('/') + next.serverName());
  REQUIRE(leftover.open(QIODevice::WriteOnly));
  leftover.close();
  CHECK(next.start({}, 500));
  CHECK(next.isPrimary());
}
#endif
