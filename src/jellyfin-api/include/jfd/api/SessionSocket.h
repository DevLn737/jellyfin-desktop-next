#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>

class QWebSocket;

namespace jfd::api {

  class JellyfinClient;

  // The session WebSocket (`/socket`). One per signed-in client; it carries
  // SyncPlay commands and group updates, remote-control commands and server
  // notifications.
  //
  // On every (re)connect it re-reports the client's session capabilities, as
  // the contract's reconnectRule requires, and only then emits `ready`.
  // Keep-alives follow the server's ForceKeepAlive timeout. A dropped
  // connection is retried with backoff (1 s doubling to 30 s) until stop().
  class SessionSocket : public QObject {
    Q_OBJECT

  public:
    enum class State { Stopped, Connecting, Registering, Ready, Reconnecting };
    Q_ENUM(State)

    explicit SessionSocket(JellyfinClient& client, QObject* parent = nullptr);
    ~SessionSocket() override;

    void start();
    void stop();
    State state() const { return m_state; }
    // Increases on each connection that reaches Ready, so subscribers can tell
    // a reconnect (where group membership may have been lost) from a repeat.
    quint64 connectionGeneration() const { return m_generation; }

    void setReconnectDelaysMs(int initialMs, int maximumMs);

    // Sends {"MessageType": type, "Data": data}.
    void send(const QString& messageType, const QJsonValue& data = {});

  Q_SIGNALS:
    void stateChanged(jfd::api::SessionSocket::State state);
    // Connected and capabilities registered.
    void ready(quint64 generation);
    void disconnected(const QString& reason);
    // Every message except keep-alives, before any typed signal.
    void messageReceived(const QString& messageType, const QJsonValue& data,
                         const QString& messageId);
    // SyncPlay messages, raw. The syncplay module parses them.
    void syncPlayCommand(const QJsonObject& data);
    void syncPlayGroupUpdate(const QJsonObject& data);

  private:
    void open();
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& text);
    void scheduleReconnect(const QString& reason);
    void setState(State state);
    QUrl socketUrl() const;

    JellyfinClient& m_client;
    QWebSocket* m_socket = nullptr;
    QTimer m_keepAlive;
    QTimer m_reconnect;
    State m_state = State::Stopped;
    quint64 m_generation = 0;
    int m_initialDelayMs = 1000;
    int m_maximumDelayMs = 30'000;
    int m_nextDelayMs = 1000;
  };

} // namespace jfd::api
