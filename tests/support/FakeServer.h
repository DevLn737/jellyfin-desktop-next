#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QWebSocket>
#include <QWebSocketServer>

#include <functional>

// A minimal HTTP/1.1 and WebSocket server on localhost for offline tests of
// the API client. Each request is answered by the handler and the connection
// is closed; WebSocket upgrades on any path go to `webSockets()`.
namespace jfd::test {

  struct HttpRequest {
    QByteArray method;
    QString path;
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers; // lower-case names
    QByteArray body;
  };

  struct HttpResponse {
    int status = 200;
    QByteArray body;
    QByteArray contentType = "application/json";
    int delayMs = 0;
    // Extra headers (Set-Cookie, Location, ...), in order.
    QList<QPair<QByteArray, QByteArray>> headers = {};
  };

  class FakeServer : public QObject {
  public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit FakeServer(Handler handler = {}) : m_handler(std::move(handler)) {
      m_tcp.listen(QHostAddress::LocalHost, 0);
      QObject::connect(&m_tcp, &QTcpServer::newConnection, this, [this]() {
        while (QTcpSocket* socket = m_tcp.nextPendingConnection())
          accept(socket);
      });
      QObject::connect(&m_ws, &QWebSocketServer::newConnection, this, [this]() {
        while (QWebSocket* socket = m_ws.nextPendingConnection()) {
          socket->setParent(this);
          m_sockets.append(socket);
          if (m_onSocket) m_onSocket(socket);
        }
      });
    }

    void setHandler(Handler handler) { m_handler = std::move(handler); }
    void onWebSocket(std::function<void(QWebSocket*)> callback) {
      m_onSocket = std::move(callback);
    }

    QUrl url(const QString& path = {}) const {
      return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_tcp.serverPort()).arg(path));
    }
    const QList<HttpRequest>& requests() const { return m_requests; }
    QList<QWebSocket*>& webSockets() { return m_sockets; }

  private:
    void accept(QTcpSocket* socket) {
      socket->setParent(this);
      QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { read(socket); });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

    void read(QTcpSocket* socket) {
      const QByteArray peeked = socket->peek(socket->bytesAvailable());
      const qsizetype headerEnd = peeked.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;
      if (peeked.left(headerEnd).toLower().contains("upgrade: websocket")) {
        socket->disconnect(this);
        m_ws.handleConnection(socket);
        return;
      }

      HttpRequest request;
      const QList<QByteArray> lines = peeked.left(headerEnd).split('\n');
      const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
      request.method = start.value(0);
      const QUrl target(QString::fromLatin1(start.value(1)));
      request.path = target.path();
      request.query = QUrlQuery(target);
      for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
          request.headers.insert(line.left(colon).toLower(), line.mid(colon + 1).trimmed());
      }
      const qsizetype length = request.headers.value("content-length", "0").toLongLong();
      if (peeked.size() < headerEnd + 4 + length) return;
      socket->read(headerEnd + 4);
      request.body = socket->read(length);
      m_requests.append(request);

      HttpResponse response = m_handler ? m_handler(request) : HttpResponse{404, {}, {}, 0};
      auto reply = [socket, response]() {
        QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
        if (!response.body.isEmpty()) out += "Content-Type: " + response.contentType + "\r\n";
        for (const auto& [name, value] : response.headers)
          out += name + ": " + value + "\r\n";
        out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
        out += "Connection: close\r\n\r\n";
        out += response.body;
        socket->write(out);
        socket->disconnectFromHost();
      };
      if (response.delayMs > 0)
        QTimer::singleShot(response.delayMs, socket, reply);
      else
        reply();
    }

    Handler m_handler;
    std::function<void(QWebSocket*)> m_onSocket;
    QTcpServer m_tcp;
    QWebSocketServer m_ws{QStringLiteral("fake"), QWebSocketServer::NonSecureMode};
    QList<HttpRequest> m_requests;
    QList<QWebSocket*> m_sockets;
  };

  // Runs the event loop until `done` returns true or the timeout passes.
  template <class Predicate> bool waitFor(Predicate done, int timeoutMs = 5000) {
    QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
      if (deadline.hasExpired()) return false;
      QEventLoop loop;
      QTimer::singleShot(5, &loop, &QEventLoop::quit);
      loop.exec();
    }
    return true;
  }

  inline HttpResponse json(const char* body, int status = 200) {
    return HttpResponse{status, QByteArray(body), "application/json", 0};
  }

} // namespace jfd::test
