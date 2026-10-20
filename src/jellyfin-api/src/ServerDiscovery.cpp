#include <jfd/api/ServerDiscovery.h>

#include <jfd/api/JellyfinClient.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QUdpSocket>

#include <algorithm>

namespace jfd::api {

  namespace {
    const QByteArray Question = QByteArrayLiteral("who is JellyfinServer?");
  }

  ServerDiscovery::ServerDiscovery(QObject* parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
      stop();
      Q_EMIT finished();
    });
  }

  ServerDiscovery::~ServerDiscovery() = default;

  void ServerDiscovery::setTargets(QList<std::pair<QHostAddress, quint16>> targets) {
    m_targets = std::move(targets);
  }

  QList<std::pair<QHostAddress, quint16>> ServerDiscovery::targets() const {
    if (!m_targets.isEmpty()) return m_targets;
    QList<std::pair<QHostAddress, quint16>> out{{QHostAddress::Broadcast, DiscoveryPort}};
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& interface : interfaces) {
      const auto flags = interface.flags();
      if (!flags.testFlag(QNetworkInterface::IsUp) ||
          !flags.testFlag(QNetworkInterface::CanBroadcast) ||
          flags.testFlag(QNetworkInterface::IsLoopBack))
        continue;
      for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
        const QHostAddress broadcast = entry.broadcast();
        if (broadcast.protocol() == QAbstractSocket::IPv4Protocol && !broadcast.isNull())
          out.append({broadcast, DiscoveryPort});
      }
    }
    return out;
  }

  void ServerDiscovery::start(int timeoutMs) {
    stop();
    m_servers.clear();
    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0)) {
      stop();
      QTimer::singleShot(0, this, &ServerDiscovery::finished);
      return;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &ServerDiscovery::readReplies);
    for (const auto& [address, port] : targets())
      m_socket->writeDatagram(Question, address, port);
    m_timeout.start(timeoutMs);
  }

  void ServerDiscovery::stop() {
    m_timeout.stop();
    if (m_socket) {
      m_socket->disconnect(this);
      m_socket->deleteLater();
      m_socket = nullptr;
    }
  }

  std::optional<DiscoveredServer> ServerDiscovery::parseReply(const QByteArray& datagram) {
    const auto document = QJsonDocument::fromJson(datagram);
    if (!document.isObject()) return std::nullopt;
    const QJsonObject o = document.object();
    DiscoveredServer server;
    server.id = o.value(QLatin1String("Id")).toString();
    server.name = o.value(QLatin1String("Name")).toString();
    const auto address =
        JellyfinClient::normalizeServerUrl(o.value(QLatin1String("Address")).toString());
    if (server.id.isEmpty() || !address) return std::nullopt;
    server.address = *address;
    return server;
  }

  void ServerDiscovery::readReplies() {
    while (m_socket && m_socket->hasPendingDatagrams()) {
      const QNetworkDatagram datagram = m_socket->receiveDatagram();
      auto server = parseReply(datagram.data());
      if (!server) continue;
      const bool known = std::any_of(m_servers.cbegin(), m_servers.cend(),
                                     [&](const DiscoveredServer& s) { return s.id == server->id; });
      if (known) continue;
      m_servers.append(*server);
      Q_EMIT serverFound(*server);
    }
  }

} // namespace jfd::api
