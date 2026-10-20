#pragma once

#include <jfd/core/PlaybackSession.h>
#include <jfd/session/ApiTransport.h>
#include <jfd/session/InputQueue.h>
#include <jfd/session/PlaybackLauncher.h>
#include <jfd/session/QtTimers.h>
#include <jfd/session/SessionHub.h>
#include <jfd/session/SleepDetector.h>
#include <jfd/session/SteadyClock.h>
#include <jfd/syncplay/GroupClient.h>

#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

namespace jfd::api {
  class JellyfinClient;
  class SessionSocket;
}

namespace jfd::session {

  // The playback session wired to a signed-in server: one input queue, Qt
  // timers on the steady clock, the SyncPlay group client on the session
  // WebSocket and the API, and the launcher that resolves items into
  // streams. The app and the headless SyncPlay agent build the same runtime;
  // only the media backend differs.
  class SessionRuntime final : public QObject {
    Q_OBJECT

  public:
    using Post = std::function<void(core::SessionInput)>;
    // Builds the media backend on the runtime's clock, reporting through post.
    using BackendFactory =
        std::function<std::unique_ptr<core::MediaBackend>(const core::Clock& clock, Post post)>;

    struct Settings {
      core::SessionSettings session;
      // Sent with every PlaybackInfo request (api::buildDeviceProfile).
      QJsonObject deviceProfile;
    };

    SessionRuntime(api::JellyfinClient& client, api::SessionSocket& socket,
                   const BackendFactory& makeBackend, Settings settings, QObject* parent = nullptr);
    ~SessionRuntime() override;

    // Thread-safe; handled in order on the runtime's thread.
    void post(core::SessionInput input) { m_queue->post(std::move(input)); }

    // Plays an item alone (outside a group, or leaving the group's item).
    void play(const QString& itemId, std::int64_t startTicks = 0);

    void createGroup(const QString& name);
    void joinGroup(const QString& groupId);
    void leaveGroup();
    // After the computer slept (noticed by the runtime itself, or reported
    // by the platform): pooled connections and the session socket are
    // likely dead without knowing, and the server has dropped the session.
    // Reconnects at once; the group client rejoins the group it was in.
    void resumeAfterSleep();

    // Leaves the group before the app quits: done runs once the server
    // answered, or after timeoutMs.
    void leaveBeforeExit(std::function<void()> done, int timeoutMs = 1500);

    SessionHub& hub() { return m_hub; }
    const core::SessionSnapshot& snapshot() const { return m_session->snapshot(); }
    const core::Clock& clock() const { return m_clock; }
    syncplay::GroupClient& group() { return *m_group; }
    PlaybackLauncher& launcher() { return *m_launcher; }
    ApiTransport& transport() { return *m_transport; }
    SleepDetector& sleepDetector() { return *m_sleep; }
    core::MediaBackend& backend() { return *m_backend; }

  Q_SIGNALS:
    // Next or Previous outside a group: the app's queue picks the item.
    void queueStepRequested(bool forward);

  private:
    void onNotice(const core::SessionNotice& notice);
    void onSocketReady(quint64 generation);
    void deliver(const char* messageType, const QJsonObject& data);
    void pollTime();

    api::JellyfinClient& m_client;
    api::SessionSocket& m_socket;
    SteadyClock m_clock;
    SessionHub m_hub;
    InputQueue* m_queue;
    QtTimers* m_timers;
    ApiTransport* m_transport;
    std::unique_ptr<syncplay::GroupClient> m_group;
    std::unique_ptr<core::MediaBackend> m_backend;
    std::unique_ptr<core::PlaybackSession> m_session;
    PlaybackLauncher* m_launcher;
    SessionHub::Subscription m_notices;
    QTimer m_timePoll;
    SleepDetector* m_sleep;
  };

} // namespace jfd::session
