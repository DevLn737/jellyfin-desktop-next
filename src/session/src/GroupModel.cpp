#include <jfd/session/GroupModel.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/api/SyncPlayApi.h>
#include <jfd/session/SessionRuntime.h>

#include <QJsonArray>
#include <QLocale>
#include <QLoggingCategory>

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace jfd::session {

  namespace {
    Q_LOGGING_CATEGORY(lcGroup, "jfd.session.group")

    // Group updates of 12.2 (GroupUpdateType) that are not refusals. Any
    // type outside these and the refusals is a newer server's.
    bool knownUpdate(const QString& type) {
      for (const char* known : {"GroupJoined", "GroupLeft", "UserJoined", "UserLeft", "StateUpdate", "PlayQueue"})
        if (type == QLatin1String(known)) return true;
      return false;
    }

    // A position change this much beyond what playing explains is a jump
    // (someone seeked); drift corrections stay well below it.
    constexpr std::int64_t JumpMs = 3000;

    QString stateName(core::GroupState state) {
      switch (state) {
      case core::GroupState::Idle: return QStringLiteral("idle");
      case core::GroupState::Waiting: return QStringLiteral("waiting");
      case core::GroupState::Paused: return QStringLiteral("paused");
      case core::GroupState::Playing: return QStringLiteral("playing");
      }
      return QStringLiteral("idle");
    }

    QString clockText(core::MediaTicks position) {
      const qint64 total = position.milliseconds() / 1000;
      const qint64 hours = total / 3600;
      const qint64 minutes = (total / 60) % 60;
      const qint64 seconds = total % 60;
      const QString mmss = QStringLiteral("%1:%2").arg(minutes, hours > 0 ? 2 : 1, 10, QLatin1Char('0'))
                               .arg(seconds, 2, 10, QLatin1Char('0'));
      return hours > 0 ? QStringLiteral("%1:%2").arg(hours).arg(mmss) : mmss;
    }

    QString rateText(double rate) { return QLocale().toString(rate, 'g', 3) + QStringLiteral("×"); }
  } // namespace

  GroupModel::GroupModel(SessionRuntime& runtime, api::JellyfinClient& client,
                         api::SessionSocket& socket, QObject* parent)
      : QObject(parent), m_runtime(runtime), m_client(client),
        m_api(std::make_unique<api::SyncPlayApi>(client)) {
    m_subscription = runtime.hub().subscribe(
        [this](const core::SessionSnapshot& snapshot) { onSnapshot(snapshot); },
        [this](const core::SessionNotice& notice) { onNotice(notice); });
    connect(&socket, &api::SessionSocket::syncPlayGroupUpdate, this, &GroupModel::onGroupUpdate);
    connect(&runtime.transport(), &ApiTransport::replied, this, &GroupModel::onReplied);
    connect(&runtime.launcher(), &PlaybackLauncher::openFailed, this,
            [this](const PlaybackLauncher::Request& request) {
              // SessionRuntime leaves the group for an item it cannot open.
              if (!request.playlistItem.isEmpty() && m_group &&
                  m_group->playlistItem == request.playlistItem)
                Q_EMIT notice(tr("The group's item could not be opened, so you left the group."), true);
            });
    checkRateSupport();
    checkAccess();
  }

  GroupModel::~GroupModel() = default;

  QString GroupModel::groupId() const {
    return m_group ? QString::fromStdString(m_group->group.value()) : QString();
  }

  QString GroupModel::groupName() const {
    return m_group ? QString::fromStdString(m_group->name) : QString();
  }

  QString GroupModel::state() const { return m_group ? stateName(m_group->state) : QString(); }

  QStringList GroupModel::participants() const {
    QStringList names;
    if (m_group)
      for (const auto& name : m_group->participants) names.append(QString::fromStdString(name));
    return names;
  }

  double GroupModel::rate() const { return m_group ? m_group->baseRate.value() : 1.0; }

  int GroupModel::driftMs() const {
    return m_group && m_group->driftMilliseconds ? int(*m_group->driftMilliseconds) : 0;
  }

  int GroupModel::pingMs() const {
    return m_group && m_group->pingMilliseconds ? int(*m_group->pingMilliseconds) : -1;
  }

  QString GroupModel::correction() const {
    if (!m_group) return {};
    switch (m_group->correction) {
    case core::SyncCorrection::Speed: return QStringLiteral("speed");
    case core::SyncCorrection::Skip: return QStringLiteral("skip");
    case core::SyncCorrection::None: break;
    }
    return {};
  }

  QVariantList GroupModel::queue() const {
    QVariantList entries;
    for (int i = 0; i < m_queue.size(); ++i) {
      const QueueEntry& entry = m_queue.at(i);
      entries.append(QVariantMap{{QStringLiteral("playlistItemId"), entry.playlistItemId},
                                 {QStringLiteral("itemId"), entry.itemId},
                                 {QStringLiteral("name"), m_names.value(entry.itemId)},
                                 {QStringLiteral("current"), i == m_queueIndex}});
    }
    return entries;
  }

  // ---- Requests ------------------------------------------------------------

  void GroupModel::createGroup(const QString& name) {
    if (!canCreate()) {
      setError(refusalText(QStringLiteral("CreateGroupDenied")));
      return;
    }
    setError({});
    startBusy();
    m_runtime.createGroup(name.trimmed().isEmpty() ? tr("Watching together") : name.trimmed());
  }

  void GroupModel::joinGroup(const QString& groupId) {
    if (!canJoin()) {
      setError(refusalText(QStringLiteral("JoinGroupDenied")));
      return;
    }
    setError({});
    startBusy();
    m_runtime.joinGroup(groupId);
  }

  void GroupModel::leaveGroup() {
    setError({});
    m_pendingQueue.reset();
    if (!m_group) return;
    startBusy();
    m_runtime.leaveGroup();
  }

  void GroupModel::watchTogether(const QString& itemId, const QString& groupName, qint64 startTicks) {
    // Without the right to create (or, in a group, to be in one) the item
    // page offers no Watch together; this catches the other ways in.
    if (m_group ? !canJoin() : !canCreate()) {
      qCInfo(lcGroup) << "Watch together refused by the user's SyncPlay access" << m_access;
      Q_EMIT notice(refusalText(QStringLiteral("CreateGroupDenied")), true);
      return;
    }
    m_pendingQueue = std::make_pair(itemId, startTicks);
    if (m_group) {
      // Already in a group: play the item there.
      onSnapshot(m_runtime.snapshot());
      return;
    }
    createGroup(groupName);
  }

  void GroupModel::playInGroup(const QStringList& itemIds, int startIndex, qint64 startTicks) {
    if (!m_group || !canJoin() || itemIds.isEmpty() || startIndex < 0 || startIndex >= itemIds.size()) return;
    m_api->setNewQueue(itemIds, startIndex, startTicks, this, [this](api::Result<api::Empty> result) {
      if (!result) setError(tr("Could not start the item in the group."));
    });
  }

  void GroupModel::queueInGroup(const QStringList& itemIds, bool next) {
    if (!m_group || !canJoin() || itemIds.isEmpty()) return;
    m_api->queue(itemIds, next, this, [this](api::Result<api::Empty> result) {
      if (!result) setError(tr("Could not add to the group's queue."));
    });
  }

  void GroupModel::setRate(double rate) {
    if (!m_rateSupported) {
      Q_EMIT notice(tr("This server cannot change the group's speed."), true);
      return;
    }
    if (const auto value = core::PlaybackRate::base(rate)) m_runtime.post(core::intent::SetRate{*value});
  }

  void GroupModel::next() { m_runtime.post(core::intent::Next{}); }

  void GroupModel::previous() { m_runtime.post(core::intent::Previous{}); }

  void GroupModel::checkRateSupport() {
    // Rate 0 changes nothing anywhere: the extension refuses it (400, or 403
    // outside a group) and a server without it has no such route (405).
    m_api->setPlaybackRate(0.0, this, [this](api::Result<api::Empty> result) {
      if (result) {
        setRateSupported(true);
      } else if (result.error().kind == api::ApiError::Kind::Unsupported) {
        setRateSupported(false);
      } else if (result.error().kind == api::ApiError::Kind::Http) {
        setRateSupported(true);
      }
    });
  }

  void GroupModel::setAccess(const QString& access) {
    if (access == m_access) return;
    m_access = access;
    qCInfo(lcGroup) << "SyncPlay access" << (access.isEmpty() ? QStringLiteral("unknown") : access);
    Q_EMIT accessChanged();
  }

  void GroupModel::checkAccess() {
    m_client.currentUser(this, [this](api::Result<api::UserInfo> user) {
      // Unanswered: keep what we had (the server still refuses on its own).
      if (user) setAccess(user->syncPlayAccess);
    });
  }

  QString GroupModel::refusalText(const QString& type) {
    if (type == QLatin1String("GroupDoesNotExist")) return tr("This room no longer exists.");
    if (type == QLatin1String("JoinGroupDenied")) return tr("You may not join this room.");
    if (type == QLatin1String("CreateGroupDenied")) return tr("You may not create rooms on this server.");
    if (type == QLatin1String("LibraryAccessDenied"))
      return tr("This room is playing something from a library you cannot see.");
    if (type == QLatin1String("NotInGroup")) return tr("You are not in this room.");
    return {};
  }

  QString GroupModel::unknownRefusalText() { return tr("The server refused. Try again later."); }

  // ---- What the session and the server tell us -----------------------------

  void GroupModel::onSnapshot(const core::SessionSnapshot& snapshot) {
    const bool wasInGroup = m_group.has_value();
    announce(snapshot);
    const bool changedGroup = m_group != snapshot.group;
    m_group = snapshot.group;
    m_position = snapshot.position;
    m_item = snapshot.item;
    m_positionAt = m_runtime.clock().now();

    if (wasInGroup != m_group.has_value()) {
      // Joined, created or left: whatever we were waiting for is done.
      if (m_busy > 0) endBusy();
      if (!m_group) {
        m_queue.clear();
        m_queueIndex = -1;
        Q_EMIT queueChanged();
      }
    }
    if (m_group && m_pendingQueue) {
      const auto [itemId, startTicks] = *m_pendingQueue;
      m_pendingQueue.reset();
      m_api->setNewQueue({itemId}, 0, startTicks, this, [this](api::Result<api::Empty> result) {
        if (!result) setError(tr("Could not start the item in the group."));
      });
    }
    if (changedGroup) Q_EMIT changed();
  }

  void GroupModel::announce(const core::SessionSnapshot& snapshot) {
    if (!m_group || !snapshot.group || m_group->group != snapshot.group->group) return;
    const core::GroupSnapshot& before = *m_group;
    const core::GroupSnapshot& after = *snapshot.group;
    if (!before.connected || !after.connected) return;

    // Participants: a multiset difference (one name per session).
    auto added = after.participants;
    auto removed = before.participants;
    for (const auto& name : before.participants) {
      const auto found = std::find(added.begin(), added.end(), name);
      if (found != added.end()) added.erase(found);
    }
    for (const auto& name : after.participants) {
      const auto found = std::find(removed.begin(), removed.end(), name);
      if (found != removed.end()) removed.erase(found);
    }
    for (const auto& name : added) Q_EMIT notice(tr("%1 joined the group").arg(QString::fromStdString(name)), false);
    for (const auto& name : removed) Q_EMIT notice(tr("%1 left the group").arg(QString::fromStdString(name)), false);

    if (after.baseRate != before.baseRate)
      Q_EMIT notice(tr("Group speed: %1").arg(rateText(after.baseRate.value())), false);
    if (before.state == core::GroupState::Playing && after.state == core::GroupState::Paused)
      Q_EMIT notice(tr("The group paused"), false);

    // A jump in the same item: further than playing since the last snapshot explains.
    if (m_positionAt && snapshot.item == m_item && !snapshot.item.isEmpty() &&
        snapshot.phase != core::PlaybackPhase::Loading) {
      const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 m_runtime.clock().now() - *m_positionAt)
                                 .count();
      const double rate = before.state == core::GroupState::Playing ? before.baseRate.value() : 0.0;
      const auto expected = m_position.milliseconds() + std::int64_t(double(elapsedMs) * rate);
      const auto jump = snapshot.position.milliseconds() - expected;
      if (std::llabs(jump) > JumpMs + elapsedMs / 10)
        Q_EMIT notice(tr("The group moved to %1").arg(clockText(snapshot.position)), false);
    }
  }

  void GroupModel::onNotice(const core::SessionNotice& notice) {
    if (std::holds_alternative<core::notice::GroupItemRequested>(notice)) {
      // Whoever started it (Play here, another member, Next): the app shows
      // the player for it.
      Q_EMIT itemOpening();
      return;
    }
    if (std::holds_alternative<core::notice::GroupItemFailed>(notice)) {
      // SessionRuntime leaves the group: the others would wait for us.
      Q_EMIT this->notice(tr("The group's item could not be played here, so you left the group."), true);
      return;
    }
    if (std::holds_alternative<core::notice::GroupOffline>(notice)) {
      Q_EMIT this->notice(tr("No connection to the group. Reconnecting; try again in a moment."), true);
      return;
    }
        if (const auto* failed = std::get_if<core::notice::GroupRequestFailed>(&notice)) {
      // Create, join and leave failures arrive as replies (onReplied).
      if (failed->request == core::group::Request::Rejoin) {
        const QString text = tr("Could not return to the group after the connection came back.");
        setError(text);
        Q_EMIT this->notice(text, true);
      } else if (failed->request == core::group::Request::Ready ||
               failed->request == core::group::Request::Buffering)
        Q_EMIT this->notice(tr("The group did not hear from this player and may be waiting for it."), true);
      return;
    }
    const auto* rejected = std::get_if<core::notice::GroupRateRejected>(&notice);
    if (!rejected) return;
    if (rejected->httpStatus == 404 || rejected->httpStatus == 405) {
      setRateSupported(false);
      Q_EMIT this->notice(tr("This server cannot change the group's speed."), true);
    } else if (rejected->unsupportedParticipants > 0) {
      Q_EMIT this->notice(tr("Not everyone in the group can play at another speed (%1 cannot).")
                              .arg(rejected->unsupportedParticipants),
                          true);
    } else {
      Q_EMIT this->notice(tr("The server refused the speed change."), true);
    }
  }

  void GroupModel::onGroupUpdate(const QJsonObject& update) {
    const QString type = update.value(QLatin1String("Type")).toString();
    if (type == QLatin1String("NotInGroup")) {
      // An answer to a request for a group we are not in: whatever we were
      // waiting for will not come.
      const bool waiting = m_busy > 0 || m_pendingQueue.has_value();
      if (m_busy > 0) endBusy();
      m_pendingQueue.reset();
      if (waiting) setError(refusalText(type));
      return;
    }
    if (const QString refusal = refusalText(type); !refusal.isEmpty()) {
      // Refusals arrive here, over the socket: Join and New answer 204.
      qCInfo(lcGroup) << "Refused:" << type;
      if (m_busy > 0) endBusy();
      m_pendingQueue.reset();
      setError(refusal);
      return;
    }
    if (!knownUpdate(type)) {
      qCWarning(lcGroup) << "Unknown group update" << type;
      // Whatever we were waiting for is not coming the usual way.
      if (m_busy > 0) {
        endBusy();
        m_pendingQueue.reset();
        setError(unknownRefusalText());
      }
      return;
    }
    if (type != QLatin1String("PlayQueue")) return;
    const QJsonObject data = update.value(QLatin1String("Data")).toObject();
    m_queue.clear();
    for (const QJsonValue& value : data.value(QLatin1String("Playlist")).toArray()) {
      const QJsonObject entry = value.toObject();
      m_queue.append({entry.value(QLatin1String("PlaylistItemId")).toString(),
                      entry.value(QLatin1String("ItemId")).toString()});
    }
    m_queueIndex = data.value(QLatin1String("PlayingItemIndex")).toInt(-1);
    Q_EMIT queueChanged();
    resolveNames();
  }

  void GroupModel::resolveNames() {
    for (const QueueEntry& entry : std::as_const(m_queue)) {
      if (entry.itemId.isEmpty() || m_names.contains(entry.itemId)) continue;
      m_names.insert(entry.itemId, QString());
      m_client.item(entry.itemId, this, [this, id = entry.itemId](api::Result<api::Item> item) {
        if (!item) return;
        const QString name = item->seriesName.isEmpty()
                                 ? item->name
                                 : item->seriesName + QStringLiteral(" – ") + item->name;
        m_names.insert(id, name);
        Q_EMIT queueChanged();
      });
    }
  }

  void GroupModel::onReplied(const QString& path, int status) {
    if (status >= 200 && status < 300) return;
    if (path == QLatin1String("/SyncPlay/Join")) {
      if (m_busy > 0) endBusy();
      setError(status == 0 ? tr("The server cannot be reached.") : tr("Could not join the group."));
    } else if (path == QLatin1String("/SyncPlay/New")) {
      if (m_busy > 0) endBusy();
      m_pendingQueue.reset();
      setError(status == 0 ? tr("The server cannot be reached.") : tr("Could not create the group."));
    } else if (path == QLatin1String("/SyncPlay/Leave")) {
      if (m_busy > 0) endBusy();
    }
  }

  // ---- State ---------------------------------------------------------------

  void GroupModel::setError(const QString& text) {
    if (text == m_errorText) return;
    m_errorText = text;
    Q_EMIT errorTextChanged();
  }

  void GroupModel::setRateSupported(bool supported) {
    if (supported == m_rateSupported) return;
    m_rateSupported = supported;
    Q_EMIT rateSupportedChanged();
  }

  void GroupModel::startBusy() {
    if (m_busy++ == 0) Q_EMIT busyChanged();
  }

  void GroupModel::endBusy() {
    if (m_busy > 0 && --m_busy == 0) Q_EMIT busyChanged();
  }

} // namespace jfd::session
