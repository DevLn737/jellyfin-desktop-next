#include <jfd/integrations/DiscordIpc.h>

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <QtEndian>

namespace jfd::integrations {

  namespace {
    constexpr quint32 MaxPayload = 64 * 1024;
    constexpr int RetryAfterAllMs = 15000;
    constexpr int RetryNextMs = 100;
    constexpr int CoalesceMs = 5000;

    enum Opcode : quint32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };
  } // namespace

  QStringList DiscordIpc::discordEndpoints() {
    QStringList endpoints;
#ifdef Q_OS_WIN
    for (int i = 0; i < 10; ++i) endpoints << QStringLiteral("discord-ipc-%1").arg(i);
#else
    // The desktop app, its Flatpak and Snap builds; then the temporary directories.
    QStringList directories;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty())
      directories << runtime << runtime + QStringLiteral("/app/com.discordapp.Discord")
                  << runtime + QStringLiteral("/snap.discord");
    for (const char* variable : {"TMPDIR", "TMP", "TEMP"}) {
      const QString value = qEnvironmentVariable(variable);
      if (!value.isEmpty()) directories << value;
    }
    directories << QStringLiteral("/tmp");
    directories.removeDuplicates();
    for (const QString& directory : directories)
      for (int i = 0; i < 10; ++i) endpoints << QDir(directory).filePath(QStringLiteral("discord-ipc-%1").arg(i));
#endif
    return endpoints;
  }

  DiscordIpc::DiscordIpc(QStringList endpoints, QObject* parent)
      : QObject(parent), m_endpoints(endpoints.isEmpty() ? discordEndpoints() : std::move(endpoints)) {
    m_retry.setSingleShot(true);
    m_timeout.setSingleShot(true);
    m_update.setSingleShot(true);
    m_socket.setReadBufferSize(MaxPayload + 8);
    connect(&m_retry, &QTimer::timeout, this, &DiscordIpc::connectNext);
    connect(&m_timeout, &QTimer::timeout, this, [this] { reconnect(RetryAfterAllMs); });
    connect(&m_update, &QTimer::timeout, this, &DiscordIpc::sendActivity);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
      const QJsonObject hello{{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), m_applicationId}};
      writeFrame(Handshake, QJsonDocument(hello).toJson(QJsonDocument::Compact));
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &DiscordIpc::readFrames);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] { reconnect(RetryAfterAllMs); });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
      // Try the next endpoint at once; wait after a full round.
      reconnect(m_endpointIndex == 0 ? RetryAfterAllMs : RetryNextMs);
    });
  }

  DiscordIpc::~DiscordIpc() {
    // Disconnect while the member timers still exist.
    shutdown();
  }

  void DiscordIpc::configure(bool enabled, const QString& applicationId) {
    const QString id = applicationId.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[0-9]{17,20}$"));
    enabled = enabled && valid.match(id).hasMatch();
    if (m_enabled == enabled && m_applicationId == id) return;
    shutdown();
    m_applicationId = id;
    m_enabled = enabled;
    m_endpointIndex = 0;
    if (m_enabled) m_retry.start(0);
  }

  void DiscordIpc::shutdown() {
    m_enabled = false;
    m_retry.stop();
    m_update.stop();
    m_timeout.stop();
    m_ready = false;
    m_socket.blockSignals(true);
    m_socket.abort();
    m_socket.blockSignals(false);
    m_input.clear();
    m_pendingNonce.clear();
    m_hasAcknowledged = false;
    m_lastWrite.invalidate();
  }

  void DiscordIpc::connectNext() {
    if (!m_enabled || m_endpoints.isEmpty()) return;
    m_timeout.start(5000);
    const QString endpoint = m_endpoints.at(m_endpointIndex);
    m_endpointIndex = (m_endpointIndex + 1) % m_endpoints.size();
    m_socket.connectToServer(endpoint, QIODevice::ReadWrite);
  }

  void DiscordIpc::reconnect(int delayMs) {
    m_ready = false;
    m_update.stop();
    m_timeout.stop();
    // abort() may emit disconnected synchronously; avoid recursive reconnects.
    m_socket.blockSignals(true);
    m_socket.abort();
    m_socket.blockSignals(false);
    m_input.clear();
    m_pendingNonce.clear();
    m_hasAcknowledged = false;
    m_lastWrite.invalidate();
    if (m_enabled) m_retry.start(delayMs);
  }

  bool DiscordIpc::writeFrame(quint32 opcode, const QByteArray& payload) {
    QByteArray frame(8, '\0');
    qToLittleEndian<quint32>(opcode, frame.data());
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), frame.data() + 4);
    frame.append(payload);
    if (m_socket.write(frame) != frame.size()) {
      reconnect(RetryAfterAllMs);
      return false;
    }
    m_socket.flush();
    return true;
  }

  void DiscordIpc::setActivity(const QJsonObject& activity) {
    m_activity = activity;
    sendActivity();
  }

  void DiscordIpc::sendActivity() {
    if (!m_ready || !m_pendingNonce.isEmpty() || (m_hasAcknowledged && m_activity == m_acknowledged)) return;
    // Coalesce rapid seeks and SyncPlay corrections. Clearing is never delayed.
    if (!m_activity.isEmpty() && m_lastWrite.isValid() && m_lastWrite.elapsed() < CoalesceMs) {
      m_update.start(static_cast<int>(CoalesceMs - m_lastWrite.elapsed()));
      return;
    }
    m_update.stop();
    m_sent = m_activity;
    m_pendingNonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonValue activity = m_sent.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_sent);
    const QJsonObject arguments{{QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid())},
                                {QStringLiteral("activity"), activity}};
    const QJsonObject payload{{QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
                              {QStringLiteral("nonce"), m_pendingNonce},
                              {QStringLiteral("args"), arguments}};
    if (writeFrame(Frame, QJsonDocument(payload).toJson(QJsonDocument::Compact))) {
      m_lastWrite.start();
      m_timeout.start(10000);
    }
  }

  void DiscordIpc::readFrames() {
    m_input.append(m_socket.readAll());
    if (m_input.size() > static_cast<qsizetype>(2 * (MaxPayload + 8))) {
      reconnect(RetryAfterAllMs);
      return;
    }
    while (m_input.size() >= 8) {
      const auto opcode = qFromLittleEndian<quint32>(m_input.constData());
      const auto length = qFromLittleEndian<quint32>(m_input.constData() + 4);
      if (length > MaxPayload || opcode > Pong) {
        reconnect(RetryAfterAllMs);
        return;
      }
      if (m_input.size() < static_cast<qsizetype>(8 + length)) return;
      const QByteArray payload = m_input.mid(8, static_cast<qsizetype>(length));
      m_input.remove(0, static_cast<qsizetype>(8 + length));
      if (opcode == Ping) {
        if (!writeFrame(Pong, payload)) return;
        continue;
      }
      if (opcode == Pong) continue;
      if (opcode != Frame) {
        reconnect(RetryAfterAllMs);
        return;
      }
      QJsonParseError error;
      const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
      if (error.error != QJsonParseError::NoError || !document.isObject()) {
        reconnect(RetryAfterAllMs);
        return;
      }
      const QJsonObject message = document.object();
      if (message.value(QStringLiteral("evt")).toString() == QStringLiteral("ERROR")) {
        // Error text can echo the activity; log only the code.
        qWarning("Discord rejected an activity update (code %d)",
                 message.value(QStringLiteral("data")).toObject().value(QStringLiteral("code")).toInt());
        reconnect(30000);
        return;
      }
      if (!m_ready && message.value(QStringLiteral("cmd")).toString() == QStringLiteral("DISPATCH") &&
          message.value(QStringLiteral("evt")).toString() == QStringLiteral("READY")) {
        m_timeout.stop();
        m_ready = true;
        sendActivity();
      } else if (!m_pendingNonce.isEmpty() &&
                 message.value(QStringLiteral("nonce")).toString() == m_pendingNonce) {
        m_timeout.stop();
        m_pendingNonce.clear();
        m_acknowledged = m_sent;
        m_hasAcknowledged = true;
        sendActivity();
      }
    }
  }

} // namespace jfd::integrations
