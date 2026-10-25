#include <jfd/session/SessionRuntime.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>

#include <QJsonDocument>
#include <QLoggingCategory>

#include <chrono>
#include <memory>
#include <utility>
#include <variant>

namespace jfd::session {

  namespace {
    // Session messages as they arrive, for tracing a group session.
    Q_LOGGING_CATEGORY(lcWire, "jfd.session.wire", QtWarningMsg)
  } // namespace

  SessionRuntime::SessionRuntime(api::JellyfinClient& client, api::SessionSocket& socket,
                                 const BackendFactory& makeBackend, Settings settings,
                                 QObject* parent)
      : QObject(parent), m_client(client), m_socket(socket) {
    m_queue = new InputQueue([this](core::SessionInput input) { m_session->post(std::move(input)); }, this);
    const Post post = [queue = m_queue](core::SessionInput input) { queue->post(std::move(input)); };
    m_timers = new QtTimers(m_clock, [post](core::TimerFired fired) { post(fired); }, this);
    m_transport = new ApiTransport(client, this);
    m_group = std::make_unique<syncplay::GroupClient>(*m_transport, m_clock, post);
    m_backend = makeBackend(m_clock, post);
    m_session = std::make_unique<core::PlaybackSession>(
        core::SessionPorts{*m_backend, m_clock, *m_timers, *m_group, m_hub}, settings.session);
    m_session->setJournal(&m_journal);
    m_hub.snapshotChanged(m_session->snapshot());
    m_launcher = new PlaybackLauncher(client, std::move(settings.deviceProfile), post, this);
    m_notices = m_hub.subscribe({}, [this](const core::SessionNotice& notice) { onNotice(notice); });

    // The socket hands over the Data objects; the group client parses whole
    // session messages, as they arrive on the wire.
    connect(&socket, &api::SessionSocket::syncPlayCommand, this,
            [this](const QJsonObject& data) { deliver("SyncPlayCommand", data); });
    connect(&socket, &api::SessionSocket::syncPlayGroupUpdate, this,
            [this](const QJsonObject& data) { deliver("SyncPlayGroupUpdate", data); });
    connect(&socket, &api::SessionSocket::ready, this, &SessionRuntime::onSocketReady);
    connect(&socket, &api::SessionSocket::disconnected, this, [this] {
      m_timePoll.stop();
      m_group->onConnectionLost();
    });

    m_sleep = new SleepDetector(this);
    connect(m_sleep, &SleepDetector::resumed, this, [this] { resumeAfterSleep(); });

    m_timePoll.setSingleShot(true);
    connect(&m_timePoll, &QTimer::timeout, this, &SessionRuntime::pollTime);
    if (socket.state() == api::SessionSocket::State::Ready) pollTime();
  }

  SessionRuntime::~SessionRuntime() {
    m_notices.reset();
    // Timers and replies must not reach a session that is gone.
    m_launcher->cancel();
    delete m_timers;
    m_timers = nullptr;
  }

  void SessionRuntime::deliver(const char* messageType, const QJsonObject& data) {
    QJsonObject message;
    message.insert(QStringLiteral("MessageType"), QLatin1String(messageType));
    message.insert(QStringLiteral("Data"), data);
    const QByteArray wire = QJsonDocument(message).toJson(QJsonDocument::Compact);
    qCDebug(lcWire).noquote() << "<=" << wire;
    m_group->onMessage(wire.toStdString());
  }

  void SessionRuntime::onSocketReady(quint64 generation) {
    // A later generation is a reconnect: the server dropped our membership
    // with the old session, and capabilities were reported again.
    if (generation > 1) m_group->onConnectionRestored();
    pollTime();
  }

  void SessionRuntime::resumeAfterSleep() {
    if (m_socket.state() == api::SessionSocket::State::Stopped) return;
    m_timePoll.stop();
    m_group->onConnectionLost();
    m_client.dropConnections();
    // stop() does not report a disconnect; the next ready() is a later
    // generation, which rejoins the group.
    m_socket.stop();
    m_socket.start();
  }

  void SessionRuntime::pollTime() {
    m_group->pollTime();
    m_timePoll.start(std::chrono::duration_cast<std::chrono::milliseconds>(m_group->nextPollDelay()));
  }

  void SessionRuntime::onNotice(const core::SessionNotice& notice) {
    if (const auto* requested = std::get_if<core::notice::GroupItemRequested>(&notice)) {
      PlaybackLauncher::Request request;
      request.itemId = QString::fromStdString(requested->item.value());
      request.startTicks = requested->position.ticks();
      request.playlistItem = requested->playlistItem;
      request.startPaused = true;
      m_launcher->open(request);
    } else if (const auto* reopen = std::get_if<core::notice::StreamReopenNeeded>(&notice)) {
      // A new stream from the requested position, for the same occurrence.
      const auto& current = m_session->snapshot();
      if (current.item.isEmpty()) return;
      PlaybackLauncher::Request request;
      request.itemId = QString::fromStdString(current.item.value());
      request.mediaSourceId = QString::fromStdString(current.mediaSource.value());
      request.startTicks = reopen->position.ticks();
      if (current.group) request.playlistItem = current.group->playlistItem;
      request.startPaused = current.phase == core::PlaybackPhase::Paused;
      m_launcher->open(request);
    } else if (const auto* step = std::get_if<core::notice::QueueStepRequested>(&notice)) {
      Q_EMIT queueStepRequested(step->forward);
    }
  }

  void SessionRuntime::play(const QString& itemId, std::int64_t startTicks) {
    PlaybackLauncher::Request request;
    request.itemId = itemId;
    request.startTicks = startTicks;
    m_launcher->open(request);
  }

  void SessionRuntime::createGroup(const QString& name) { m_group->createGroup(name.toStdString()); }

  void SessionRuntime::joinGroup(const QString& groupId) {
    m_group->joinGroup(core::GroupId(groupId.toStdString()));
  }

  void SessionRuntime::leaveGroup() { m_group->leaveGroup(); }

  void SessionRuntime::leaveBeforeExit(std::function<void()> done, int timeoutMs) {
    auto once = std::make_shared<std::function<void()>>(std::move(done));
    const auto finish = [once] {
      if (!*once) return;
      auto callback = std::move(*once);
      *once = nullptr;
      callback();
    };
    if (!m_group->isInGroup()) {
      finish();
      return;
    }
    m_client.rawRequest(api::JellyfinClient::Method::Post, QStringLiteral("/SyncPlay/Leave"), {}, this,
                        [finish](int, QByteArray) { finish(); });
    QTimer::singleShot(timeoutMs, this, finish);
  }

} // namespace jfd::session
