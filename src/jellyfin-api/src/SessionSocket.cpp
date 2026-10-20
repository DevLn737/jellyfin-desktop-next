#include <jfd/api/SessionSocket.h>

#include <jfd/api/JellyfinClient.h>

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QWebSocket>

#include <algorithm>

namespace jfd::api {

  namespace {
    const QString KeepAlive = QStringLiteral("KeepAlive");
    const QString ForceKeepAlive = QStringLiteral("ForceKeepAlive");
  } // namespace

  SessionSocket::SessionSocket(JellyfinClient& client, QObject* parent)
      : QObject(parent), m_client(client) {
    m_keepAlive.setSingleShot(false);
    connect(&m_keepAlive, &QTimer::timeout, this, [this]() { send(KeepAlive); });
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &SessionSocket::open);
  }

  SessionSocket::~SessionSocket() {
    if (m_socket) {
      m_socket->disconnect(this);
      m_socket->abort();
    }
  }

  void SessionSocket::setReconnectDelaysMs(int initialMs, int maximumMs) {
    m_initialDelayMs = std::max(1, initialMs);
    m_maximumDelayMs = std::max(m_initialDelayMs, maximumMs);
    m_nextDelayMs = m_initialDelayMs;
  }

  void SessionSocket::start() {
    if (m_state != State::Stopped) return;
    m_nextDelayMs = m_initialDelayMs;
    open();
  }

  void SessionSocket::stop() {
    m_reconnect.stop();
    m_keepAlive.stop();
    if (m_socket) {
      m_socket->disconnect(this);
      m_socket->close();
      m_socket->deleteLater();
      m_socket = nullptr;
    }
    setState(State::Stopped);
  }

  QUrl SessionSocket::socketUrl() const {
    QUrl url = m_client.url(QStringLiteral("socket"));
    url.setScheme(url.scheme() == QLatin1String("https") ? QStringLiteral("wss")
                                                         : QStringLiteral("ws"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("deviceId"), m_client.identity().deviceId);
    url.setQuery(query);
    return url;
  }

  void SessionSocket::open() {
    if (m_socket) {
      m_socket->disconnect(this);
      m_socket->abort();
      m_socket->deleteLater();
    }
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, &SessionSocket::onConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &SessionSocket::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &SessionSocket::onTextMessage);

    // The token goes in the Authorization header with the same identity as
    // HTTP requests, so the server attaches the socket to this session and
    // the token never appears in a URL.
    QNetworkRequest request(socketUrl());
    request.setRawHeader("Authorization", m_client.authorizationHeader());
    setState(m_generation == 0 && m_state != State::Reconnecting ? State::Connecting
                                                                 : State::Reconnecting);
    m_socket->open(request);
  }

  void SessionSocket::onConnected() {
    setState(State::Registering);
    const auto& capabilities = m_client.capabilities();
    if (!capabilities) {
      m_nextDelayMs = m_initialDelayMs;
      setState(State::Ready);
      Q_EMIT ready(++m_generation);
      return;
    }
    // A reconnect may come with a fresh server session, which starts without
    // capabilities; report them again before anyone relies on the session.
    m_client.reportCapabilities(*capabilities, m_socket, [this](Result<Empty> result) {
      if (!result) {
        if (m_socket) m_socket->abort();
        return;
      }
      m_nextDelayMs = m_initialDelayMs;
      setState(State::Ready);
      Q_EMIT ready(++m_generation);
    });
  }

  void SessionSocket::onDisconnected() {
    m_keepAlive.stop();
    QString reason = m_socket ? m_socket->closeReason() : QString();
    if (m_socket && reason.isEmpty()) reason = m_socket->errorString();
    Q_EMIT disconnected(reason);
    scheduleReconnect(reason);
  }

  void SessionSocket::scheduleReconnect(const QString&) {
    if (m_state == State::Stopped) return;
    setState(State::Reconnecting);
    m_reconnect.start(m_nextDelayMs);
    m_nextDelayMs = std::min(m_nextDelayMs * 2, m_maximumDelayMs);
  }

  void SessionSocket::onTextMessage(const QString& text) {
    const auto document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isObject()) return;
    const QJsonObject message = document.object();
    const QString type = message.value(QLatin1String("MessageType")).toString();
    const QJsonValue data = message.value(QLatin1String("Data"));

    if (type == ForceKeepAlive) {
      // Data is the server's lost-connection timeout in seconds; the web
      // client answers at half of it, and so does this one.
      const int timeoutSeconds = data.isDouble() ? data.toInt() : 60;
      send(KeepAlive);
      m_keepAlive.start(std::max(1, timeoutSeconds) * 500);
      return;
    }
    if (type == KeepAlive) return;

    Q_EMIT messageReceived(type, data, message.value(QLatin1String("MessageId")).toString());
    if (type == QLatin1String("SyncPlayCommand"))
      Q_EMIT syncPlayCommand(data.toObject());
    else if (type == QLatin1String("SyncPlayGroupUpdate"))
      Q_EMIT syncPlayGroupUpdate(data.toObject());
  }

  void SessionSocket::send(const QString& messageType, const QJsonValue& data) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    QJsonObject message{{QStringLiteral("MessageType"), messageType}};
    if (!data.isUndefined() && !data.isNull()) message.insert(QStringLiteral("Data"), data);
    m_socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
  }

  void SessionSocket::setState(State state) {
    if (m_state == state) return;
    m_state = state;
    Q_EMIT stateChanged(state);
  }

} // namespace jfd::api
