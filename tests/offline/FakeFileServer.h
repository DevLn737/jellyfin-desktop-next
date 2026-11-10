#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <functional>

// A file server on localhost for the download tests: GET <any path> answers
// the file registered for that path, with byte ranges (Range, If-Range,
// 206, 416) as a real server does, and ways to misbehave: cut the body short
// and close, ignore ranges, change the file, answer an error status,
// redirect, or send slowly.
namespace jfd::test {

  struct FileRequest {
    QString path;
    QByteArray range;
    QByteArray ifRange;
    QByteArray authorization;
    int status = 0;
    // Body bytes actually written for this request.
    qint64 bodyBytes = 0;
  };

  class FakeFileServer : public QObject {
  public:
    struct File {
      QByteArray content;
      QByteArray etag = "\"v1\"";
      QByteArray lastModified = "Mon, 02 Nov 2026 10:00:00 GMT";
    };

    FakeFileServer() {
      m_tcp.listen(QHostAddress::LocalHost, 0);
      QObject::connect(&m_tcp, &QTcpServer::newConnection, this, [this]() {
        while (QTcpSocket* socket = m_tcp.nextPendingConnection()) {
          socket->setParent(this);
          QObject::connect(socket, &QTcpSocket::readyRead, this,
                           [this, socket]() { read(socket); });
          QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
      });
    }

    QUrl url(const QString& path = {}) const {
      return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_tcp.serverPort()).arg(path));
    }

    void setFile(const QString& path, File file) { m_files.insert(path, std::move(file)); }
    File& file(const QString& path) { return m_files[path]; }

    // Answer 200 with the whole file whatever the Range header says.
    bool ignoreRanges = false;
    // Stop the next `cutTimes` bodies after `cutAfter` bytes (the connection
    // stalls until the client drops it).
    qint64 cutAfter = -1;
    int cutTimes = 0;
    // Close the connection after a cut instead of stalling. Qt may then send
    // the same request again by itself, so request counts vary.
    bool closeOnCut = false;
    // Answer the next `failTimes` requests (any number with -1) with
    // `failStatus` and no file.
    int failStatus = 0;
    int failTimes = 0;
    // Answer 302 to this absolute URL instead.
    QByteArray redirectTo;
    // Send the body `chunkBytes` at a time, one chunk every `chunkIntervalMs`.
    int chunkBytes = 0;
    int chunkIntervalMs = 0;
    // Called with each request before it is answered.
    std::function<void(const FileRequest&)> onRequest;

    const QList<FileRequest>& requests() const { return m_requests; }
    qint64 bodyBytesSent() const {
      qint64 total = 0;
      for (const auto& r : m_requests)
        total += r.bodyBytes;
      return total;
    }

    static QByteArray sha256(const QByteArray& data) {
      return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    }

  private:
    void read(QTcpSocket* socket) {
      if (socket->property("answered").toBool()) return;
      const QByteArray peeked = socket->peek(socket->bytesAvailable());
      const qsizetype headerEnd = peeked.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;
      socket->read(headerEnd + 4);
      socket->setProperty("answered", true);

      const QList<QByteArray> lines = peeked.left(headerEnd).split('\n');
      const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
      FileRequest request;
      request.path = QUrl(QString::fromLatin1(start.value(1))).path();
      for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QByteArray name = line.left(colon).toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "range") request.range = value;
        if (name == "if-range") request.ifRange = value;
        if (name == "authorization") request.authorization = value;
      }
      m_requests.append(request);
      const qsizetype index = m_requests.size() - 1;
      if (onRequest) onRequest(request);

      QByteArray head;
      QByteArray body;
      int status = 200;
      if (failTimes != 0) {
        if (failTimes > 0) --failTimes;
        status = failStatus;
      } else if (!redirectTo.isEmpty()) {
        status = 302;
        head += "Location: " + redirectTo + "\r\n";
      } else if (!m_files.contains(request.path)) {
        status = 404;
      } else {
        const File& f = m_files[request.path];
        const qint64 size = f.content.size();
        head += "ETag: " + f.etag + "\r\nLast-Modified: " + f.lastModified +
                "\r\nAccept-Ranges: bytes\r\n";
        qint64 first = 0;
        const bool validatorMatches = request.ifRange.isEmpty() || request.ifRange == f.etag ||
                                      request.ifRange == f.lastModified;
        if (!ignoreRanges && request.range.startsWith("bytes=") && validatorMatches) {
          first = request.range.mid(6).split('-').value(0).toLongLong();
          if (first >= size) {
            status = 416;
            head += "Content-Range: bytes */" + QByteArray::number(size) + "\r\n";
          } else {
            status = 206;
            head += "Content-Range: bytes " + QByteArray::number(first) + '-' +
                    QByteArray::number(size - 1) + '/' + QByteArray::number(size) + "\r\n";
          }
        }
        if (status != 416) body = f.content.mid(first);
      }
      m_requests[index].status = status;

      QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n" + head;
      out += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
      socket->write(out);

      qint64 limit = body.size();
      bool cut = false;
      if (cutTimes > 0 && cutAfter >= 0 && (status == 200 || status == 206)) {
        --cutTimes;
        cut = cutAfter < limit;
        limit = std::min<qint64>(limit, cutAfter);
      }
      send(QPointer<QTcpSocket>(socket), index, body.left(limit), cut);
    }

    void send(QPointer<QTcpSocket> socket, qsizetype index, QByteArray body, bool cut) {
      if (!socket) return;
      if (chunkBytes <= 0 || body.size() <= chunkBytes) {
        socket->write(body);
        m_requests[index].bodyBytes += body.size();
        // A cut answer stalls instead: the connection stays open with no
        // more data until the client gives up on it (its stall timeout).
        // Closing it would let Qt send the same request again by itself
        // (QHttpNetworkConnectionChannel resends after some closes), which
        // the server would count as a second download.
        if (!cut || closeOnCut) socket->disconnectFromHost();
        return;
      }
      socket->write(body.left(chunkBytes));
      m_requests[index].bodyBytes += chunkBytes;
      QTimer::singleShot(chunkIntervalMs, this,
                         [this, socket, index, cut, rest = body.mid(chunkBytes)]() {
                           send(socket, index, rest, cut);
                         });
    }

    QTcpServer m_tcp;
    QHash<QString, File> m_files;
    QList<FileRequest> m_requests;
  };

  inline QByteArray patternBytes(qint64 size, quint32 seed) {
    QByteArray data(size, Qt::Uninitialized);
    quint32 x = seed * 2654435761u + 1;
    for (qint64 i = 0; i < size; ++i) {
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      data[i] = static_cast<char>(x & 0xff);
    }
    return data;
  }

} // namespace jfd::test
