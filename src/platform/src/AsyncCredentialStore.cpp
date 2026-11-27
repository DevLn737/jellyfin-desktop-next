#include <jfd/platform/AsyncCredentialStore.h>

#include <QCoreApplication>
#include <QEvent>
#include <QMetaObject>
#include <QPointer>

namespace jfd::platform {

  namespace {
    // Runs done on the receiver's thread, unless the caller's context went
    // away meanwhile. Called on the worker thread.
    template <typename Result, typename Done>
    void deliver(QObject* receiver, const QPointer<QObject>& context, bool hasContext, Done done,
                 Result result) {
      if (!done) return;
      QMetaObject::invokeMethod(
          receiver,
          [context, hasContext, done = std::move(done), result = std::move(result)]() mutable {
            if (hasContext && context.isNull()) return;
            done(std::move(result));
          },
          Qt::QueuedConnection);
    }
  } // namespace

  AsyncCredentialStore::AsyncCredentialStore(std::unique_ptr<CredentialStore> store)
      : m_owned(store ? std::move(store) : createUnavailableCredentialStore()), m_store(*m_owned) {
    start();
  }

  AsyncCredentialStore::AsyncCredentialStore(CredentialStore& store) : m_store(store) {
    start();
  }

  AsyncCredentialStore::~AsyncCredentialStore() {
    {
      const std::lock_guard lock(m_mutex);
      m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
  }

  void AsyncCredentialStore::start() {
    m_receiver = std::make_unique<QObject>();
    m_thread = std::thread([this] { run(); });
  }

  void AsyncCredentialStore::post(std::function<void()> job) {
    {
      const std::lock_guard lock(m_mutex);
      m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
  }

  void AsyncCredentialStore::run() {
    std::unique_lock lock(m_mutex);
    for (;;) {
      m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
      // Calls made before the store goes are still carried out.
      if (m_jobs.empty()) return;
      std::function<void()> job = std::move(m_jobs.front());
      m_jobs.pop_front();
      ++m_running;
      lock.unlock();
      job();
      lock.lock();
      --m_running;
      if (m_jobs.empty() && m_running == 0) m_idle.notify_all();
    }
  }

  void AsyncCredentialStore::read(const QString& key, QObject* context, ReadDone done) {
    QObject* receiver = m_receiver.get();
    post([this, receiver, key, context = QPointer<QObject>(context), hasContext = context != nullptr,
          done = std::move(done)]() mutable {
      deliver(receiver, context, hasContext, std::move(done), m_store.read(key));
    });
  }

  void AsyncCredentialStore::write(const QString& key, const QString& secret, QObject* context,
                                   StatusDone done) {
    QObject* receiver = m_receiver.get();
    post([this, receiver, key, secret = QString(secret), context = QPointer<QObject>(context),
          hasContext = context != nullptr, done = std::move(done)]() mutable {
      deliver(receiver, context, hasContext, std::move(done), m_store.write(key, secret));
      // The worker's copy of the secret is not kept around.
      secret.fill(QChar(0));
    });
  }

  void AsyncCredentialStore::remove(const QString& key, QObject* context, StatusDone done) {
    QObject* receiver = m_receiver.get();
    post([this, receiver, key, context = QPointer<QObject>(context), hasContext = context != nullptr,
          done = std::move(done)]() mutable {
      deliver(receiver, context, hasContext, std::move(done), m_store.remove(key));
    });
  }

  bool AsyncCredentialStore::finishPending(std::chrono::milliseconds timeout) {
    bool idle = false;
    {
      std::unique_lock lock(m_mutex);
      idle = m_idle.wait_for(lock, timeout, [this] { return m_jobs.empty() && m_running == 0; });
    }
    // The answers of the calls that finished, in order.
    QCoreApplication::sendPostedEvents(m_receiver.get(), QEvent::MetaCall);
    return idle;
  }

} // namespace jfd::platform
