#pragma once

#include <jfd/platform/CredentialStore.h>

#include <QObject>
#include <QString>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace jfd::platform {

  // A credential store used off the GUI thread. The system stores answer
  // synchronously and may take long: the Secret Service goes over D-Bus and
  // a locked keyring shows an unlock prompt that waits for the user; the
  // Windows Credential Manager may stall behind antivirus or a roaming
  // profile. Every call here returns at once; the store is called on one
  // worker thread, one call after another in the order they were made (a
  // write and the read after it see each other), and the answer comes back
  // on the thread that asked, through its event loop.
  //
  // The store is used only by the worker from then on (a store that is
  // also read elsewhere, as the tests' memory store, must be thread-safe).
  class AsyncCredentialStore {
  public:
    using ReadDone = std::function<void(CredentialStore::ReadResult)>;
    using StatusDone = std::function<void(CredentialStore::Status)>;

    explicit AsyncCredentialStore(std::unique_ptr<CredentialStore> store);
    // A store that outlives this object (tests).
    explicit AsyncCredentialStore(CredentialStore& store);
    // Finishes the calls already made (a token written just before quitting
    // is kept); their answers are not delivered any more.
    ~AsyncCredentialStore();

    AsyncCredentialStore(const AsyncCredentialStore&) = delete;
    AsyncCredentialStore& operator=(const AsyncCredentialStore&) = delete;

    // done runs on the calling thread unless context is gone by then.
    void read(const QString& key, QObject* context, ReadDone done);
    void write(const QString& key, const QString& secret, QObject* context = nullptr,
               StatusDone done = {});
    // Removing a missing key succeeds.
    void remove(const QString& key, QObject* context = nullptr, StatusDone done = {});

    // Waits up to timeout for the calls made so far, then runs the answers
    // that have come back. From the thread that made the calls, where that
    // thread's event loop is not running yet (start-up) or in tests. True
    // when nothing is left to wait for.
    bool finishPending(std::chrono::milliseconds timeout);

  private:
    void start();
    void post(std::function<void()> job);
    void run();

    std::unique_ptr<CredentialStore> m_owned;
    CredentialStore& m_store;
    // Lives on the thread that created this store; the answers are posted
    // to it and run there.
    std::unique_ptr<QObject> m_receiver;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idle;
    std::deque<std::function<void()>> m_jobs;
    int m_running = 0;
    bool m_stop = false;
    std::thread m_thread;
  };

} // namespace jfd::platform
