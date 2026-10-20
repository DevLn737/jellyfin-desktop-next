#include <jfd/platform/SingleInstance.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

namespace jfd::platform {

  namespace {
    // Message: protocol version, then the arguments as a QStringList.
    constexpr quint32 ProtocolVersion = 1;
    constexpr qint64 MaxMessageBytes = 64 * 1024;

    QString userName() {
      QString user = qEnvironmentVariable("USER");
      if (user.isEmpty()) user = qEnvironmentVariable("USERNAME");
      return user;
    }

    QString defaultKey() {
      return QCoreApplication::applicationName() + QLatin1Char('|') + userName();
    }
  } // namespace

  SingleInstance::SingleInstance(QString key, QObject* parent) : QObject(parent) {
    if (key.isEmpty()) key = defaultKey();
    // Short and free of characters that are not allowed in pipe or socket names.
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
    m_name = QStringLiteral("jfd-") + QString::fromLatin1(hash.toHex().left(24));
  }

  SingleInstance::~SingleInstance() = default;

  bool SingleInstance::isPrimary() const { return m_server && m_server->isListening(); }

  bool SingleInstance::start(const QStringList& arguments, int timeoutMs) {
    if (isPrimary()) return true;
    if (sendToPrimary(arguments, timeoutMs)) return false;

    m_server = std::make_unique<QLocalServer>();
    // Only this user may connect (Unix socket permissions, pipe ACL on Windows).
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_name)) {
      // Nobody answered above, so the socket is left over from a crash.
      QLocalServer::removeServer(m_name);
      if (!m_server->listen(m_name)) {
        // Another instance may have won a start race; hand over once more.
        if (sendToPrimary(arguments, timeoutMs)) {
          m_server.reset();
          return false;
        }
        qWarning("Single instance: cannot listen on %s: %s", qPrintable(m_name),
                 qPrintable(m_server->errorString()));
        // Run anyway; a second window is better than none.
        m_server.reset();
        return true;
      }
    }
    connect(m_server.get(), &QLocalServer::newConnection, this, &SingleInstance::acceptConnections);
    return true;
  }

  bool SingleInstance::sendToPrimary(const QStringList& arguments, int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(timeoutMs)) return false;
    QByteArray message;
    QDataStream stream(&message, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << ProtocolVersion << arguments;
    socket.write(message);
    if (!socket.waitForBytesWritten(timeoutMs)) return false;
    // The primary closes the connection once it has read the message.
    socket.waitForDisconnected(timeoutMs);
    return true;
  }

  void SingleInstance::acceptConnections() {
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
      connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
      connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
        if (socket->bytesAvailable() > MaxMessageBytes) {
          socket->abort();
          return;
        }
        QDataStream stream(socket);
        stream.setVersion(QDataStream::Qt_6_0);
        stream.startTransaction();
        quint32 version = 0;
        QStringList arguments;
        stream >> version >> arguments;
        if (!stream.commitTransaction()) return; // Wait for the rest.
        socket->disconnectFromServer();
        if (version == ProtocolVersion) Q_EMIT activated(arguments);
      });
    }
  }

} // namespace jfd::platform
