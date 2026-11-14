#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

// The offline watch journal: what was watched while the server could not
// be told, kept in one file of JSON lines until the server has it.
//
// Two kinds of line:
//   - "event": one playback report that did not reach the server (no
//     connection, or no answer), and every report of a downloaded copy
//     played without the server. `playId` groups the reports of one
//     playback; its last one counts.
//   - "update": the state the journal sends for an item
//     (UpdateUserItemDataDto), computed at the first attempt and written
//     before it is sent. A lost answer and a second attempt send the same
//     values, so PlayCount is never counted twice.
//
// Times are the server's: the system clock corrected by the last known
// clock offset (server minus system, from SyncPlay's TimeSync), which is
// stored with each event.
//
// Events are appended; other changes rewrite the file atomically
// (QSaveFile). Lines that do not parse are dropped at load.
namespace jfd::offline {

  struct ViewEvent {
    QString serverId;
    QString userId;
    QString itemId;
    QString playId;
    std::int64_t positionTicks = 0;
    // The playback ended (a Stopped report).
    bool stopped = false;
    // Server time of the event, ms since the Unix epoch.
    std::int64_t atMs = 0;
    // Server minus system clock when it was recorded; clockKnown false: no
    // measurement was ever made, `atMs` is the system clock.
    std::int64_t clockOffsetMs = 0;
    bool clockKnown = false;
  };

  struct ViewUpdate {
    QString serverId;
    QString userId;
    QString itemId;
    // UpdateUserItemDataDto: PlaybackPositionTicks, Played, PlayCount,
    // LastPlayedDate.
    QJsonObject body;
    // Server time of the latest playback it carries, ms since the epoch:
    // a server LastPlayedDate later than this wins.
    std::int64_t atMs = 0;
  };

  class ViewJournal {
  public:
    // Loads `path` if it exists. The folder is created on the first write.
    explicit ViewJournal(QString path);

    const QString& path() const { return m_path; }
    bool isEmpty() const { return m_events.isEmpty() && m_updates.isEmpty(); }

    bool append(const ViewEvent& event);
    QList<ViewEvent> events() const { return m_events; }
    QList<ViewUpdate> updates() const { return m_updates; }
    QList<ViewEvent> events(const QString& serverId, const QString& userId) const;
    QList<ViewUpdate> updates(const QString& serverId, const QString& userId) const;
    std::optional<ViewUpdate> update(const QString& serverId, const QString& userId,
                                     const QString& itemId) const;
    bool hasPlay(const QString& playId) const;

    // A report of that playback reached the server after all: its earlier
    // events are stale.
    bool removePlay(const QString& playId);
    // Stores the item's update (replacing an older one) and removes the
    // events it was computed from, in one write.
    bool setUpdate(const ViewUpdate& update, const QStringList& consumedPlays);
    // Forgets the item: its update and the given plays (the server has
    // them, or a newer state of its own).
    bool removeItem(const QString& serverId, const QString& userId, const QString& itemId,
                    const QStringList& plays);

    // The last known clock offset (server minus system, ms); kept in the
    // file, so events recorded after a restart without the server still use
    // it. Written when it moves by more than a second.
    std::optional<std::int64_t> clockOffsetMs() const { return m_clockOffsetMs; }
    void setClockOffsetMs(std::int64_t offset);

  private:
    void load();
    bool rewrite();

    QString m_path;
    QList<ViewEvent> m_events;
    QList<ViewUpdate> m_updates;
    std::optional<std::int64_t> m_clockOffsetMs;
  };

  // How a playback counts on the server, by Jellyfin's default rules
  // (MinResumePct 5, MaxResumePct 90, MinResumeDurationSeconds 300): an
  // administrator may have changed them, and a client without admin rights
  // cannot read them.
  enum class ViewOutcome {
    // Under 5 %: nothing is saved.
    Nothing,
    // Between: the position is saved.
    Resume,
    // Over 90 %, or any position past 5 % of an item under 300 s: played.
    Played,
  };
  ViewOutcome classifyView(std::int64_t positionTicks, std::int64_t runTimeTicks);

} // namespace jfd::offline
