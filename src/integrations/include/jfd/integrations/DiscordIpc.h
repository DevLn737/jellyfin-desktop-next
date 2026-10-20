#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Discord's local RPC: framed JSON over a named pipe (Windows) or a Unix
// socket (Linux), no Discord SDK. Only SET_ACTIVITY is used; updates are
// coalesced to one per five seconds, clearing is never delayed, and the latest
// activity is sent again after a reconnect.
namespace jfd::integrations {

  class DiscordIpc final : public QObject {
    Q_OBJECT
  public:
    // endpoints: socket names or paths tried in turn; empty means the places
    // Discord listens on this platform (discordEndpoints()).
    explicit DiscordIpc(QStringList endpoints = {}, QObject* parent = nullptr);
    ~DiscordIpc() override;

    // applicationId is the public Application ID from the Discord Developer
    // Portal (17-20 digits); anything else keeps the connection closed.
    void configure(bool enabled, const QString& applicationId);
    // An empty object clears the activity.
    void setActivity(const QJsonObject& activity);
    // Closes the connection, which removes this process's activity.
    void shutdown();
    bool isReady() const { return m_ready; }

    static QStringList discordEndpoints();

  private:
    void connectNext();
    void reconnect(int delayMs);
    void readFrames();
    void sendActivity();
    bool writeFrame(quint32 opcode, const QByteArray& payload);

    QLocalSocket m_socket;
    QTimer m_retry;
    QTimer m_timeout;
    QTimer m_update;
    QElapsedTimer m_lastWrite;
    QByteArray m_input;
    QStringList m_endpoints;
    QString m_applicationId;
    QString m_pendingNonce;
    QJsonObject m_activity;
    QJsonObject m_sent;
    QJsonObject m_acknowledged;
    qsizetype m_endpointIndex = 0;
    bool m_enabled = false;
    bool m_ready = false;
    bool m_hasAcknowledged = false;
  };

} // namespace jfd::integrations
