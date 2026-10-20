#pragma once

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <optional>
#include <utility>

class QUdpSocket;

namespace jfd::api {

  struct DiscoveredServer {
    QString id;
    QString name;
    // The address the server reports for itself, normalized like a typed one.
    QUrl address;
  };

  // Finds Jellyfin servers on the local network: a UDP broadcast of
  // "who is JellyfinServer?" to port 7359, which every server with auto
  // discovery enabled (the default) answers with its id, name and address.
  // Each server is reported once per search, however many interfaces it
  // answers on.
  class ServerDiscovery : public QObject {
    Q_OBJECT

  public:
    static constexpr quint16 DiscoveryPort = 7359;

    explicit ServerDiscovery(QObject* parent = nullptr);
    ~ServerDiscovery() override;

    // Starts a search that ends after `timeoutMs`; a search already running
    // is restarted and its results are dropped.
    void start(int timeoutMs = 3000);
    void stop();
    bool isRunning() const { return m_socket != nullptr; }
    const QList<DiscoveredServer>& servers() const { return m_servers; }

    // Where the question is sent. Defaults to the broadcast address of every
    // IPv4 interface that has one, plus 255.255.255.255; tests point it at a
    // local responder.
    void setTargets(QList<std::pair<QHostAddress, quint16>> targets);

    // Parses one answer; nullopt when it is not a Jellyfin discovery reply.
    static std::optional<DiscoveredServer> parseReply(const QByteArray& datagram);

  Q_SIGNALS:
    void serverFound(const jfd::api::DiscoveredServer& server);
    void finished();

  private:
    void readReplies();
    QList<std::pair<QHostAddress, quint16>> targets() const;

    QUdpSocket* m_socket = nullptr;
    QTimer m_timeout;
    QList<std::pair<QHostAddress, quint16>> m_targets;
    QList<DiscoveredServer> m_servers;
  };

} // namespace jfd::api
