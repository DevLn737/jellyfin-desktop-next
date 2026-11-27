#include "../support/DoctestQt.h"

#include <jfd/platform/AsyncCredentialStore.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace jfd::platform;
using namespace std::chrono_literals;
using Status = CredentialStore::Status;

namespace {
  // Waits on every call and notes the thread it was called on.
  class SlowStore final : public CredentialStore {
  public:
    explicit SlowStore(std::chrono::milliseconds delay) : m_delay(delay) {}
    bool isAvailable() const override { return true; }
    ReadResult read(const QString& key) const override {
      note();
      return m_store.read(key);
    }
    Status write(const QString& key, const QString& secret) override {
      note();
      return m_store.write(key, secret);
    }
    Status remove(const QString& key) override {
      note();
      return m_store.remove(key);
    }
    mutable std::atomic<Qt::HANDLE> thread = nullptr;
    mutable std::atomic<int> calls = 0;

  private:
    void note() const {
      thread = QThread::currentThreadId();
      std::this_thread::sleep_for(m_delay);
      ++calls;
    }
    std::chrono::milliseconds m_delay;
    MemoryCredentialStore m_store;
  };

  template <class Predicate> bool waitFor(Predicate done, int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
      if (timer.elapsed() > timeoutMs) return false;
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
  }
} // namespace

TEST_CASE("Credential calls return at once and answer on the calling thread, in order") {
  SlowStore slow(150ms);
  AsyncCredentialStore queue(slow);
  QObject context;
  QElapsedTimer timer;
  timer.start();
  QStringList answers;
  Qt::HANDLE answeredOn = nullptr;
  queue.write(QStringLiteral("k"), QStringLiteral("a"), &context,
              [&](Status status) { answers.append(status == Status::Ok ? QStringLiteral("w") : QString()); });
  queue.read(QStringLiteral("k"), &context, [&](CredentialStore::ReadResult result) {
    answeredOn = QThread::currentThreadId();
    answers.append(result.secret);
  });
  queue.remove(QStringLiteral("k"));
  queue.read(QStringLiteral("k"), &context, [&](CredentialStore::ReadResult result) {
    answers.append(result.status == Status::NotFound ? QStringLiteral("gone") : result.secret);
  });
  CHECK(timer.elapsed() < 100);
  REQUIRE(waitFor([&] { return answers.size() == 3; }));
  CHECK(answers == QStringList{QStringLiteral("w"), QStringLiteral("a"), QStringLiteral("gone")});
  CHECK(answeredOn == QThread::currentThreadId());
  CHECK(slow.thread.load() != QThread::currentThreadId());
}

TEST_CASE("An answer for a context that went away is dropped") {
  SlowStore slow(100ms);
  AsyncCredentialStore queue(slow);
  bool answered = false;
  auto context = std::make_unique<QObject>();
  queue.read(QStringLiteral("k"), context.get(), [&](CredentialStore::ReadResult) { answered = true; });
  context.reset();
  REQUIRE(queue.finishPending(5s));
  QCoreApplication::processEvents();
  CHECK_FALSE(answered);
}

TEST_CASE("finishPending waits a bounded time and runs the answers that came") {
  SlowStore slow(300ms);
  AsyncCredentialStore queue(slow);
  QObject context;
  bool answered = false;
  queue.read(QStringLiteral("k"), &context, [&](CredentialStore::ReadResult) { answered = true; });
  // Too short: the store is still busy.
  CHECK_FALSE(queue.finishPending(20ms));
  CHECK_FALSE(answered);
  // Long enough: the answer runs inside the call, no event loop needed.
  CHECK(queue.finishPending(5s));
  CHECK(answered);
}

TEST_CASE("Writes made just before the store goes are still carried out") {
  SlowStore slow(50ms);
  {
    AsyncCredentialStore queue(slow);
    queue.write(QStringLiteral("a"), QStringLiteral("1"));
    queue.write(QStringLiteral("b"), QStringLiteral("2"));
    queue.remove(QStringLiteral("a"));
  }
  CHECK(slow.calls == 3);
  CHECK(slow.read(QStringLiteral("a")).status == Status::NotFound);
  CHECK(slow.read(QStringLiteral("b")).secret == QStringLiteral("2"));
}

TEST_CASE("An owned store defaults to the unavailable one") {
  AsyncCredentialStore queue(std::unique_ptr<CredentialStore>{});
  Status status = Status::Ok;
  QObject context;
  queue.write(QStringLiteral("k"), QStringLiteral("v"), &context, [&](Status s) { status = s; });
  REQUIRE(queue.finishPending(5s));
  CHECK(status == Status::Unavailable);
}
