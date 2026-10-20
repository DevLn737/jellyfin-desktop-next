#pragma once

#include <QDBusConnection>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

namespace jfd::test {

  // A dbus-daemon of its own for one test: fake portal and screen saver
  // services and the code under test talk on it, isolated from the session.
  class PrivateBus {
  public:
    PrivateBus() {
      const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
      if (daemon.isEmpty()) return;
      m_daemon.start(daemon, {QStringLiteral("--session"), QStringLiteral("--nofork"),
                              QStringLiteral("--print-address=1")});
      if (!m_daemon.waitForStarted(5000) || !m_daemon.waitForReadyRead(5000)) return;
      m_address = QString::fromUtf8(m_daemon.readLine().trimmed());
    }
    ~PrivateBus() {
      for (const QString& name : m_names) QDBusConnection::disconnectFromBus(name);
      m_daemon.kill();
      m_daemon.waitForFinished(5000);
    }
    PrivateBus(const PrivateBus&) = delete;
    PrivateBus& operator=(const PrivateBus&) = delete;

    bool isRunning() const { return !m_address.isEmpty(); }

    // A separate connection per role, like separate processes.
    QDBusConnection connect(const QString& name) {
      m_names.append(name);
      return QDBusConnection::connectToBus(m_address, name);
    }

  private:
    QProcess m_daemon;
    QString m_address;
    QStringList m_names;
  };

} // namespace jfd::test
