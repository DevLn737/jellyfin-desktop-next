#include "../support/DoctestQt.h"

#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>

#include <QJsonDocument>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>

using namespace jfd::api;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;

namespace {
  struct Fixture {
    FakeServer server{[](const HttpRequest&) { return HttpResponse{204, {}, {}, 0}; }};
    JellyfinClient client{ClientIdentity{QStringLiteral("Test"), QStringLiteral("1.0"),
                                         QStringLiteral("Device"), QStringLiteral("device1")}};
    QList<QWebSocket*> sockets;
    QStringList received;
    QList<QByteArray> authorizations;

    Fixture() {
      client.setServerUrl(server.url());
      client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
      server.onWebSocket([this](QWebSocket* socket) {
        sockets.append(socket);
        authorizations.append(socket->request().rawHeader("Authorization"));
        QObject::connect(socket, &QWebSocket::textMessageReceived, socket,
                         [this](const QString& text) { received.append(text); });
      });
    }

    int capabilityReports() const {
      int count = 0;
      for (const auto& r : server.requests())
        if (r.path == QLatin1String("/Sessions/Capabilities/Full")) ++count;
      return count;
    }
  };

  void sendFromServer(QWebSocket* socket, const char* text) {
    socket->sendTextMessage(QString::fromUtf8(text));
  }

  // A TCP relay in front of the fake server. silence() makes the
  // connections open so far swallow everything in both directions without
  // closing: a half-open connection after a Wi-Fi roam or a NAT timeout,
  // where even WebSocket pings go unanswered. Later connections relay.
  class BlackHoleProxy : public QObject {
  public:
    explicit BlackHoleProxy(quint16 target) : m_target(target) {
      m_server.listen(QHostAddress::LocalHost, 0);
      QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* client = m_server.nextPendingConnection()) relay(client);
      });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort())); }
    void silence() {
      for (auto* pipe : std::as_const(m_pipes)) pipe->dead = true;
    }

  private:
    struct Pipe : QObject {
      bool dead = false;
    };
    void relay(QTcpSocket* client) {
      auto* pipe = new Pipe;
      pipe->setParent(this);
      m_pipes.append(pipe);
      client->setParent(pipe);
      auto* upstream = new QTcpSocket(pipe);
      upstream->connectToHost(QHostAddress::LocalHost, m_target);
      const auto forward = [pipe](QTcpSocket* from, QTcpSocket* to) {
        const QByteArray data = from->readAll();
        if (!pipe->dead && to->state() == QAbstractSocket::ConnectedState) to->write(data);
      };
      QObject::connect(client, &QTcpSocket::readyRead, pipe, [=] {
        if (upstream->state() == QAbstractSocket::ConnectedState) forward(client, upstream);
      });
      QObject::connect(upstream, &QTcpSocket::connected, pipe, [=] { forward(client, upstream); });
      QObject::connect(upstream, &QTcpSocket::readyRead, pipe, [=] { forward(upstream, client); });
      QObject::connect(client, &QTcpSocket::disconnected, pipe, [=] {
        if (!pipe->dead) upstream->disconnectFromHost();
      });
      QObject::connect(upstream, &QTcpSocket::disconnected, pipe, [=] {
        if (!pipe->dead) client->disconnectFromHost();
      });
    }

    QTcpServer m_server;
    quint16 m_target;
    QList<Pipe*> m_pipes;
  };
} // namespace

TEST_CASE("The socket authenticates by header and re-reports capabilities on every connect") {
  Fixture f;
  SessionCapabilities capabilities;
  capabilities.supportsSyncPlayPlaybackRate = true;
  bool reported = false;
  f.client.reportCapabilities(capabilities, nullptr, [&](Result<Empty> r) { reported = r.ok(); });
  REQUIRE(waitFor([&] { return reported; }));

  SessionSocket socket(f.client);
  socket.setReconnectDelaysMs(50, 200);
  QSignalSpy ready(&socket, &SessionSocket::ready);
  socket.start();
  REQUIRE(waitFor([&] { return ready.count() == 1; }));
  CHECK(socket.state() == SessionSocket::State::Ready);
  CHECK(f.capabilityReports() == 2);
  REQUIRE(f.authorizations.size() == 1);
  CHECK(f.authorizations.front().contains("Token=\"token1\""));
  CHECK_FALSE(f.sockets.front()->requestUrl().query().contains(QStringLiteral("token1")));

  // The server drops the connection; the client reconnects and registers again.
  f.sockets.front()->close();
  REQUIRE(waitFor([&] { return ready.count() == 2; }));
  CHECK(ready.at(1).at(0).toULongLong() == 2);
  CHECK(f.capabilityReports() == 3);
  CHECK(f.sockets.size() == 2);
  socket.stop();
  CHECK(socket.state() == SessionSocket::State::Stopped);
}

TEST_CASE("ForceKeepAlive starts keep-alives and SyncPlay messages are routed") {
  Fixture f;
  SessionSocket socket(f.client);
  QSignalSpy commands(&socket, &SessionSocket::syncPlayCommand);
  QSignalSpy updates(&socket, &SessionSocket::syncPlayGroupUpdate);
  QSignalSpy messages(&socket, &SessionSocket::messageReceived);
  QSignalSpy ready(&socket, &SessionSocket::ready);
  socket.start();
  REQUIRE(waitFor([&] { return ready.count() == 1; }));
  QWebSocket* server = f.sockets.front();

  sendFromServer(server, R"({"MessageType": "ForceKeepAlive", "Data": 1})");
  // One immediately, then every half timeout.
  REQUIRE(waitFor([&] { return f.received.size() >= 3; }, 3000));
  CHECK(QJsonDocument::fromJson(f.received.front().toUtf8())
            .object()
            .value(QStringLiteral("MessageType"))
            .toString() == QStringLiteral("KeepAlive"));

  sendFromServer(server, R"({"MessageType": "SyncPlayCommand", "MessageId": "m1",
    "Data": {"GroupId": "g1", "Command": "Unpause", "PositionTicks": 10, "When": "2026-10-20T07:05:02Z"}})");
  sendFromServer(server, R"({"MessageType": "SyncPlayGroupUpdate",
    "Data": {"GroupId": "g1", "Type": "StateUpdate", "Data": {"State": "Playing"}}})");
  sendFromServer(server, R"({"MessageType": "KeepAlive"})");
  sendFromServer(server, "not json");
  REQUIRE(waitFor([&] { return updates.count() == 1; }));
  REQUIRE(commands.count() == 1);
  CHECK(commands.at(0).at(0).toJsonObject().value(QStringLiteral("Command")).toString() ==
        QStringLiteral("Unpause"));
  // Keep-alives are handled inside the socket and never reach subscribers.
  CHECK(messages.count() == 2);
  CHECK(messages.at(0).at(2).toString() == QStringLiteral("m1"));
  socket.stop();
}

TEST_CASE("A connection that goes silent is aborted after 2.5 keep-alive intervals and reconnects") {
  Fixture f;
  BlackHoleProxy proxy(static_cast<quint16>(f.server.url().port()));
  f.client.setServerUrl(proxy.url());
  SessionSocket socket(f.client);
  socket.setReconnectDelaysMs(50, 200);
  QSignalSpy ready(&socket, &SessionSocket::ready);
  QSignalSpy disconnected(&socket, &SessionSocket::disconnected);
  socket.start();
  REQUIRE(waitFor([&] { return ready.count() == 1; }));
  QWebSocket* server = f.sockets.front();

  // Keep-alives every 500 ms. The fake server never answers them, but its
  // WebSocket layer answers the pings: the connection stays.
  sendFromServer(server, R"({"MessageType": "ForceKeepAlive", "Data": 1})");
  QElapsedTimer alive;
  alive.start();
  REQUIRE(waitFor([&] { return alive.elapsed() > 2500; }, 4000));
  CHECK(disconnected.count() == 0);
  CHECK(socket.state() == SessionSocket::State::Ready);

  // Now nothing comes back any more, and nothing reports a close.
  proxy.silence();
  QElapsedTimer silent;
  silent.start();
  REQUIRE(waitFor([&] { return disconnected.count() == 1; }, 4000));
  // 2.5 intervals of 500 ms after the last frame heard.
  CHECK(silent.elapsed() >= 600);
  CHECK(silent.elapsed() < 2500);
  // The usual reconnect follows: a later generation (the group rejoins).
  REQUIRE(waitFor([&] { return ready.count() == 2; }));
  CHECK(ready.at(1).at(0).toULongLong() == 2);
  CHECK(f.sockets.size() == 2);
  socket.stop();
}
