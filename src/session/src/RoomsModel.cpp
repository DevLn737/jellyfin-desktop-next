#include <jfd/session/RoomsModel.h>

#include <jfd/api/SyncPlayApi.h>
#include <jfd/session/GroupModel.h>

#include <QLoggingCategory>

#include <algorithm>
#include <utility>

namespace jfd::session {

  namespace {
    Q_LOGGING_CATEGORY(lcRooms, "jfd.session.rooms")
  } // namespace

  RoomsModel::RoomsModel(GroupModel& group, api::JellyfinClient& client, QObject* parent)
      : QAbstractListModel(parent), m_group(group), m_api(std::make_unique<api::SyncPlayApi>(client)) {
    m_poll.setInterval(DefaultPollInterval);
    // Coarse timers may fire 5 % late; the 6 s promise has room for latency only.
    m_poll.setTimerType(Qt::PreciseTimer);
    connect(&m_poll, &QTimer::timeout, this, &RoomsModel::refresh);
    connect(&group, &GroupModel::accessChanged, this, [this] {
      Q_EMIT accessChanged();
      if (!available()) clear();
      updatePolling();
    });
  }

  RoomsModel::~RoomsModel() = default;

  int RoomsModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

  QVariant RoomsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rooms.size()) return {};
    const Room& room = m_rooms.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return room.name;
    case IdRole: return room.id;
    case StateRole: return room.state;
    case ParticipantsRole: return room.participants;
    case ParticipantCountRole: return int(room.participants.size());
    default: return {};
    }
  }

  QHash<int, QByteArray> RoomsModel::roleNames() const {
    return {{IdRole, "roomId"},
            {NameRole, "name"},
            {StateRole, "roomState"},
            {ParticipantsRole, "participants"},
            {ParticipantCountRole, "participantCount"}};
  }

  bool RoomsModel::available() const { return m_group.canJoin(); }
  bool RoomsModel::canJoin() const { return m_group.canJoin(); }
  bool RoomsModel::canCreate() const { return m_group.canCreate(); }

  void RoomsModel::setActive(bool active) {
    if (active == m_active) return;
    m_active = active;
    Q_EMIT activeChanged();
    updatePolling();
  }

  void RoomsModel::setOnline(bool online) {
    if (online == m_online) return;
    m_online = online;
    Q_EMIT onlineChanged();
    updatePolling();
  }

  void RoomsModel::setPollInterval(std::chrono::milliseconds interval) {
    m_poll.setInterval(interval);
  }

  std::chrono::milliseconds RoomsModel::pollInterval() const { return m_poll.intervalAsDuration(); }

  void RoomsModel::updatePolling() {
    const bool poll = m_active && m_online && available();
    if (poll == m_poll.isActive()) return;
    if (!poll) {
      m_poll.stop();
      return;
    }
    // Back on the tab: the list now, not one interval later.
    m_poll.start();
    refresh();
  }

  void RoomsModel::refresh() {
    if (m_loading || !available()) return;
    m_loading = true;
    Q_EMIT loadingChanged();
    m_api->listGroups(this, [this, serial = m_serial](api::Result<QList<api::SyncPlayGroup>> result) {
      if (serial != m_serial) return;
      m_loading = false;
      m_failed = !result;
      if (!result) {
        qCInfo(lcRooms) << "SyncPlay/List failed:" << result.error().message;
        Q_EMIT loadingChanged();
        return;
      }
      m_loaded = true;
      QList<Room> rooms;
      rooms.reserve(result->size());
      for (const api::SyncPlayGroup& group : *result)
        rooms.append({group.groupId, group.groupName, group.state.toLower(), group.participants});
      apply(std::move(rooms));
      Q_EMIT loadingChanged();
    });
  }

  void RoomsModel::apply(QList<Room> rooms) {
    const QVariantMap selectedBefore = selected();
    const bool goneBefore = selectedGone();
    const int countBefore = count();

    // Gone rooms first, from the end.
    for (qsizetype row = m_rooms.size() - 1; row >= 0; --row) {
      const QString& id = m_rooms.at(row).id;
      const bool kept = std::any_of(rooms.cbegin(), rooms.cend(), [&id](const Room& room) { return room.id == id; });
      if (kept) continue;
      beginRemoveRows({}, int(row), int(row));
      m_rooms.removeAt(row);
      endRemoveRows();
    }
    // Then in the server's order: move, update or insert each.
    for (qsizetype row = 0; row < rooms.size(); ++row) {
      const Room& room = rooms.at(row);
      qsizetype at = row;
      while (at < m_rooms.size() && m_rooms.at(at).id != room.id) ++at;
      if (at == m_rooms.size()) {
        beginInsertRows({}, int(row), int(row));
        m_rooms.insert(row, room);
        endInsertRows();
        continue;
      }
      if (at != row) {
        beginMoveRows({}, int(at), int(at), {}, int(row));
        m_rooms.move(at, row);
        endMoveRows();
      }
      if (!(m_rooms.at(row) == room)) {
        m_rooms[row] = room;
        const QModelIndex index = this->index(int(row));
        Q_EMIT dataChanged(index, index);
      }
    }

    if (count() != countBefore) Q_EMIT countChanged();
    if (selected() != selectedBefore || selectedGone() != goneBefore) Q_EMIT selectedChanged();
  }

  QVariantMap RoomsModel::toMap(const Room& room) {
    return {{QStringLiteral("id"), room.id},
            {QStringLiteral("name"), room.name},
            {QStringLiteral("state"), room.state},
            {QStringLiteral("participants"), room.participants},
            {QStringLiteral("participantCount"), int(room.participants.size())}};
  }

  QVariantMap RoomsModel::get(int row) const {
    if (row < 0 || row >= m_rooms.size()) return {};
    return toMap(m_rooms.at(row));
  }

  int RoomsModel::indexOf(const QString& roomId) const {
    for (qsizetype row = 0; row < m_rooms.size(); ++row)
      if (m_rooms.at(row).id == roomId) return int(row);
    return -1;
  }

  QVariantMap RoomsModel::selected() const { return get(indexOf(m_selectedId)); }

  bool RoomsModel::selectedGone() const { return !m_selectedId.isEmpty() && indexOf(m_selectedId) < 0; }

  void RoomsModel::select(const QString& roomId) {
    if (roomId == m_selectedId) return;
    m_selectedId = roomId;
    Q_EMIT selectedChanged();
  }

  void RoomsModel::join(const QString& roomId) {
    if (roomId.isEmpty()) return;
    // GroupModel says why when the user may not join.
    m_group.joinGroup(roomId);
  }

  void RoomsModel::create(const QString& name) { m_group.createGroup(name); }

  void RoomsModel::clear() {
    ++m_serial;
    const bool loadState = m_loading || m_loaded || m_failed;
    m_loading = false;
    m_loaded = false;
    m_failed = false;
    if (!m_rooms.isEmpty()) {
      beginResetModel();
      m_rooms.clear();
      endResetModel();
      Q_EMIT countChanged();
    }
    if (loadState) Q_EMIT loadingChanged();
    if (!m_selectedId.isEmpty()) {
      m_selectedId.clear();
      Q_EMIT selectedChanged();
    }
  }

} // namespace jfd::session
