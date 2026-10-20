#pragma once

#include <QObject>
#include <QStringList>

#include <memory>

class QLocalServer;

// One running copy per user. The first instance listens on a local socket
// (a named pipe on Windows, a socket in the runtime directory on Linux); a
// second start hands its arguments to the first and exits, and the first
// raises its window.
namespace jfd::platform {

  class SingleInstance final : public QObject {
    Q_OBJECT
  public:
    // key names the instance; the default is the application name and the
    // user, so different users on one machine each get their own.
    explicit SingleInstance(QString key = {}, QObject* parent = nullptr);
    ~SingleInstance() override;

    // Becomes the primary instance, or sends arguments to the running one.
    // Returns true when this process is the primary and should continue; false
    // when another instance received the arguments and this one should exit.
    // A stale socket from a crashed instance is removed.
    bool start(const QStringList& arguments, int timeoutMs = 2000);
    bool isPrimary() const;
    QString serverName() const { return m_name; }

  Q_SIGNALS:
    // Another start of the application; raise the window and handle arguments.
    void activated(const QStringList& arguments);

  private:
    bool sendToPrimary(const QStringList& arguments, int timeoutMs);
    void acceptConnections();

    QString m_name;
    std::unique_ptr<QLocalServer> m_server;
  };

} // namespace jfd::platform
