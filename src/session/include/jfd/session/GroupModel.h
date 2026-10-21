#pragma once

#include <jfd/session/SessionHub.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>
#include <optional>

namespace jfd::api {
  class JellyfinClient;
  class SessionSocket;
  class SyncPlayApi;
} // namespace jfd::api

namespace jfd::session {

  class SessionRuntime;

  // Watching together, for the group screen and the group OSD in the
  // player: the server's groups, creating, joining and leaving one, the
  // group's speed and queue, and how well we keep up. Everything that plays
  // goes through the session runtime; this model only asks the server and
  // reads the session's snapshots.
  //
  // notice() reports what the group did (someone joined, the group paused,
  // jumped or changed speed, a speed was refused) as one line for a toast.
  class GroupModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool inGroup READ inGroup NOTIFY changed)
    // False while our connection to the group is lost (we rejoin on our own).
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString groupId READ groupId NOTIFY changed)
    Q_PROPERTY(QString groupName READ groupName NOTIFY changed)
    // "idle", "waiting", "paused", "playing".
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // User names, one per session.
    Q_PROPERTY(QStringList participants READ participants NOTIFY changed)
    Q_PROPERTY(double rate READ rate NOTIFY changed)
    // Whether the server has the playback-rate extension; speed controls
    // are hidden when it does not.
    Q_PROPERTY(bool rateSupported READ rateSupported NOTIFY rateSupportedChanged)
    // How far behind the group we are (negative: ahead), while playing.
    Q_PROPERTY(bool hasDrift READ hasDrift NOTIFY changed)
    Q_PROPERTY(int driftMs READ driftMs NOTIFY changed)
    // "", "speed" or "skip": how we are catching up right now.
    Q_PROPERTY(QString correction READ correction NOTIFY changed)
    // The group's queue: [{playlistItemId, itemId, name, current}].
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    // The server's groups: [{id, name, state, participants}].
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

  public:
    GroupModel(SessionRuntime& runtime, api::JellyfinClient& client, api::SessionSocket& socket,
               QObject* parent = nullptr);
    ~GroupModel() override;

    bool inGroup() const { return m_group.has_value(); }
    bool connected() const { return m_group && m_group->connected; }
    QString groupId() const;
    QString groupName() const;
    QString state() const;
    QStringList participants() const;
    double rate() const;
    bool rateSupported() const { return m_rateSupported; }
    bool hasDrift() const { return m_group && m_group->driftMilliseconds.has_value(); }
    int driftMs() const;
    QString correction() const;
    QVariantList queue() const;
    QVariantList groups() const { return m_groups; }
    bool busy() const { return m_busy > 0; }
    QString errorText() const { return m_errorText; }

    Q_INVOKABLE void refreshGroups();
    Q_INVOKABLE void createGroup(const QString& name);
    Q_INVOKABLE void joinGroup(const QString& groupId);
    Q_INVOKABLE void leaveGroup();
    // Creates a group and starts it on this item ("Watch together").
    Q_INVOKABLE void watchTogether(const QString& itemId, const QString& groupName,
                                   qint64 startTicks = 0);
    Q_INVOKABLE void setRate(double rate);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // Asks the server whether it has the playback-rate extension. Called on
    // construction; again after signing in to another server.
    Q_INVOKABLE void checkRateSupport();

  Q_SIGNALS:
    void changed();
    void queueChanged();
    void groupsChanged();
    void busyChanged();
    void errorTextChanged();
    void rateSupportedChanged();
    void notice(const QString& text, bool error);

  private:
    void onSnapshot(const core::SessionSnapshot& snapshot);
    void onNotice(const core::SessionNotice& notice);
    void onGroupUpdate(const QJsonObject& update);
    void onReplied(const QString& path, int status);
    void announce(const core::SessionSnapshot& snapshot);
    void resolveNames();
    void setError(const QString& text);
    void setRateSupported(bool supported);
    void startBusy();
    void endBusy();

    struct QueueEntry {
      QString playlistItemId;
      QString itemId;
    };

    SessionRuntime& m_runtime;
    api::JellyfinClient& m_client;
    std::unique_ptr<api::SyncPlayApi> m_api;
    SessionHub::Subscription m_subscription;
    std::optional<core::GroupSnapshot> m_group;
    core::MediaTicks m_position;
    core::ItemId m_item;
    std::optional<core::LocalInstant> m_positionAt;
    QList<QueueEntry> m_queue;
    int m_queueIndex = -1;
    QHash<QString, QString> m_names;
    QVariantList m_groups;
    int m_busy = 0;
    QString m_errorText;
    bool m_rateSupported = false;
    // Set while we create a group for watchTogether: the item to queue once
    // the server put us in it.
    std::optional<std::pair<QString, qint64>> m_pendingQueue;
  };

} // namespace jfd::session
