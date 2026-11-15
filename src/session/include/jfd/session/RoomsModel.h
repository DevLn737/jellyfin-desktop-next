#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <chrono>
#include <memory>

namespace jfd::api {
  class JellyfinClient;
  class SyncPlayApi;
} // namespace jfd::api

namespace jfd::session {

  class GroupModel;

  // Watching together as rooms: the server's SyncPlay groups as a live list
  // for the rooms tab, a room's card before joining, joining and creating.
  // A room is a group of the server; joining, leaving and everything inside
  // one belong to GroupModel, which this model hands the requests to.
  //
  // The list is asked for (SyncPlay/List) right away when the tab becomes
  // `active` (shown, and the window active) and then every pollInterval
  // (5 s) while it stays so, never while hidden, offline or without the
  // right to join. Each answer is compared with the rows: a room that did
  // not change keeps its row (no signal at all), a changed one gets
  // dataChanged, new and gone rooms are inserted and removed.
  //
  // The access properties follow GroupModel's (the user's SyncPlayAccess).
  class RoomsModel : public QAbstractListModel {
    Q_OBJECT
    // Set by the rooms page: the tab is shown and the window is active.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // Whether the server can be reached (LibraryUi.online); no polling
    // without it.
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY onlineChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // A list request is on its way.
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // A list arrived (since signing in, or since the last clear()).
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadingChanged)
    // The last list request failed (the rows stay as they were).
    Q_PROPERTY(bool failed READ failed NOTIFY loadingChanged)
    // The room whose card is open, by id ("" for none), and its data:
    // {id, name, state, participants, participantCount}; empty when the
    // room is gone (selectedGone).
    Q_PROPERTY(QString selectedId READ selectedId WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(bool selectedGone READ selectedGone NOTIFY selectedChanged)
    Q_PROPERTY(bool available READ available NOTIFY accessChanged)
    Q_PROPERTY(bool canJoin READ canJoin NOTIFY accessChanged)
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY accessChanged)

  public:
    enum Role {
      IdRole = Qt::UserRole + 1,
      NameRole,
      // "idle", "waiting", "paused", "playing".
      StateRole,
      // User names, one per session.
      ParticipantsRole,
      ParticipantCountRole,
    };
    Q_ENUM(Role)

    static constexpr std::chrono::milliseconds DefaultPollInterval{5000};

    RoomsModel(GroupModel& group, api::JellyfinClient& client, QObject* parent = nullptr);
    ~RoomsModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool active() const { return m_active; }
    void setActive(bool active);
    bool online() const { return m_online; }
    void setOnline(bool online);
    int count() const { return int(m_rooms.size()); }
    bool loading() const { return m_loading; }
    bool loaded() const { return m_loaded; }
    bool failed() const { return m_failed; }
    QString selectedId() const { return m_selectedId; }
    QVariantMap selected() const;
    bool selectedGone() const;
    bool available() const;
    bool canJoin() const;
    bool canCreate() const;

    // For tests: the time between list requests.
    void setPollInterval(std::chrono::milliseconds interval);
    std::chrono::milliseconds pollInterval() const;

    // Asks for the list now (also while not active, e.g. a Refresh button).
    Q_INVOKABLE void refresh();
    // Opens a room's card ("" closes it).
    Q_INVOKABLE void select(const QString& roomId);
    // GroupModel's join and create, within the user's rights.
    Q_INVOKABLE void join(const QString& roomId);
    Q_INVOKABLE void create(const QString& name);
    // {id, name, ...} of a row, for QML; empty when there is none.
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString& roomId) const;
    // Forgets the rooms and the open card (signing out, another server).
    void clear();

  Q_SIGNALS:
    void activeChanged();
    void onlineChanged();
    void countChanged();
    void loadingChanged();
    void selectedChanged();
    void accessChanged();

  private:
    struct Room {
      QString id;
      QString name;
      QString state;
      QStringList participants;
      bool operator==(const Room&) const = default;
    };

    void updatePolling();
    void apply(QList<Room> rooms);
    static QVariantMap toMap(const Room& room);

    GroupModel& m_group;
    std::unique_ptr<api::SyncPlayApi> m_api;
    QList<Room> m_rooms;
    QTimer m_poll;
    bool m_active = false;
    bool m_online = true;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_failed = false;
    QString m_selectedId;
    // Drops answers to requests sent before clear().
    quint64 m_serial = 0;
  };

} // namespace jfd::session
