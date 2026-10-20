#pragma once

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/Models.h>
#include <jfd/core/Timers.h>

#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>

// SyncPlay HTTP routes, as the 12.2 server and its playback-rate extension
// define them. This is transport only: the syncplay module decides when to
// send what. Messages from the group arrive over SessionSocket.
namespace jfd::api {

  struct SyncPlayReadyState {
    // Server time at which `positionTicks` was observed.
    core::ServerInstant when;
    // Real absolute media position, including stream start offsets.
    std::int64_t positionTicks = 0;
    // Whether playback is actually unpaused.
    bool isPlaying = false;
    // The current queue occurrence (PlaylistItemId), not the item id.
    QString playlistItemId;
  };

  class SyncPlayApi {
  public:
    explicit SyncPlayApi(JellyfinClient& client) : m_client(client) {}

    void createGroup(const QString& groupName, QObject* context, Callback<SyncPlayGroup> done);
    // Fails with isPlaybackRateConflict() when the group plays at a non-1x
    // rate and this session did not advertise SupportsSyncPlayPlaybackRate.
    void joinGroup(const QString& groupId, QObject* context, Callback<Empty> done);
    void leaveGroup(QObject* context, Callback<Empty> done);
    void listGroups(QObject* context, Callback<QList<SyncPlayGroup>> done);
    void group(const QString& groupId, QObject* context, Callback<SyncPlayGroup> done);

    void setNewQueue(const QStringList& itemIds, int playingItemPosition,
                     std::int64_t startPositionTicks, QObject* context, Callback<Empty> done);
    void setPlaylistItem(const QString& playlistItemId, QObject* context, Callback<Empty> done);

    void unpause(QObject* context, Callback<Empty> done);
    void pause(QObject* context, Callback<Empty> done);
    void stop(QObject* context, Callback<Empty> done);
    void seek(std::int64_t positionTicks, QObject* context, Callback<Empty> done);
    // POST SyncPlay/SetPlaybackRate (extension). Fails with Unsupported on a
    // server without the route, with isPlaybackRateConflict() for a non-1x
    // rate when a participant lacks support, and with Http 400 outside
    // 0.25..5. A rate of exactly 1 is always accepted by the extension.
    void setPlaybackRate(double rate, QObject* context, Callback<Empty> done);

    void ready(const SyncPlayReadyState& state, QObject* context, Callback<Empty> done);
    void buffering(const SyncPlayReadyState& state, QObject* context, Callback<Empty> done);
    void setIgnoreWait(bool ignoreWait, QObject* context, Callback<Empty> done);
    // Reports the measured round trip so the server can delay commands for
    // slow members.
    void ping(std::int64_t pingMs, QObject* context, Callback<Empty> done);

    // GET GetUtcTime, with send and receive times read from `clock` (the
    // session's monotonic clock). `clock` must outlive the request.
    void utcTime(const core::Clock& clock, QObject* context, Callback<UtcTimeSample> done);

  private:
    JellyfinClient& m_client;
  };

} // namespace jfd::api
