#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/integrations/MsiUpdater.h>
#include <jfd/integrations/SignedDownload.h>

#include <QDir>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

using namespace jfd::integrations;
using jfd::test::waitFor;

namespace {
  constexpr const char* testKey = "RWRrLMuGH6cumqwldRaRJzhfK+oDeMgMd6r0LKTeST4p273XcqTT65OJ";

  // Answers every request with the headers and the first bytes of a large
  // file, then sends nothing more and keeps the connection open: a download
  // that stalls half way, as on a dropped mobile link.
  class StallingServer : public QObject {
  public:
    StallingServer() {
      m_tcp.listen(QHostAddress::LocalHost, 0);
      QObject::connect(&m_tcp, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* socket = m_tcp.nextPendingConnection()) {
          socket->setParent(this);
          QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
            if (!socket->readAll().contains("\r\n\r\n")) return;
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                          "Content-Length: 1000000\r\n\r\n");
            socket->write(QByteArray(1000, 'x'));
          });
        }
      });
    }
    QUrl url(const QString& path) const {
      return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_tcp.serverPort()).arg(path));
    }

  private:
    QTcpServer m_tcp;
  };
} // namespace

TEST_CASE("An update download has an inactivity timeout by default") {
  QNetworkAccessManager network;
  SignedDownload download(&network);
  CHECK(download.transferTimeout() == SignedDownload::defaultTransferTimeout);
  CHECK(SignedDownload::defaultTransferTimeout > std::chrono::milliseconds(0));
  CHECK(SignedDownload::defaultTransferTimeout <= std::chrono::minutes(2));
}

TEST_CASE("A stalled update download fails, leaves nothing behind, and a later one can start") {
  StallingServer server;
  QTemporaryDir dir;
  QNetworkAccessManager network;
  SignedDownload download(&network);
  download.setTransferTimeout(std::chrono::milliseconds(300));
  const QString path = dir.filePath(QStringLiteral("update.bin"));
  const ReleaseAsset file{QStringLiteral("update.bin"), server.url(QStringLiteral("/a")), 1000000};
  const ReleaseAsset signature{QStringLiteral("update.bin.minisig"), server.url(QStringLiteral("/s")), 300};

  for (int round = 0; round < 2; ++round) {
    CAPTURE(round);
    QString error;
    bool done = false;
    QElapsedTimer timer;
    timer.start();
    download.start(file, signature, path, QStringLiteral("update.bin"), *parsePublicKey(testKey),
                   [&](const QString& reason) {
                     error = reason;
                     done = true;
                   });
    REQUIRE(waitFor([&] { return done; }, 10'000));
    CHECK(timer.elapsed() >= 250);
    CHECK_MESSAGE(error.startsWith(QStringLiteral("download stalled")), error.toStdString());
    CHECK_FALSE(QFile::exists(path));
    CHECK_FALSE(QFile::exists(path + QStringLiteral(".minisig")));
  }
}
