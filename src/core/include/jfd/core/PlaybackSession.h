#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/MediaBackend.h>
#include <jfd/core/Readiness.h>
#include <jfd/core/Reconciler.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/SessionSnapshot.h>
#include <jfd/core/SyncPlay.h>
#include <jfd/core/Timeline.h>
#include <jfd/core/Timers.h>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

// The single owner of playback state (ADR 0003). One queue of inputs; stale
// inputs are dropped on entry; only the session commands the backend and
// speaks for this client in the SyncPlay group.
namespace jfd::core {

  struct SessionPorts {
    MediaBackend& backend;
    Clock& clock;
    Timers& timers;
    SyncPlayOutbox& syncPlay;
    SessionObserver& observer;
  };

  struct SessionSettings {
    ReconcilerSettings reconciler;
    // How long a load or group seek may take to become ready (contract: 30 s).
    WallTicks readyDeadline = std::chrono::seconds(30);
    // Correction pauses after a command or a skip, as in Jellyfin Web
    // (half the maximum speed-correction drift).
    WallTicks correctionCooldown = std::chrono::milliseconds(1500);
  };

  class PlaybackSession {
  public:
    explicit PlaybackSession(SessionPorts ports, SessionSettings settings = {});

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    // Queues an input and, unless already handling one, handles the queue.
    void post(SessionInput input);

    const SessionSnapshot& snapshot() const { return m_snapshot; }
    std::uint64_t droppedInputs() const { return m_dropped; }

  private:
    enum class ReadyPurpose { None, Local, GroupOpen, GroupSeek, Sync };

    struct ExternalTrackRequest {
      RequestId request;
      ExternalTrackState state;
      bool issued = false;
    };

    struct Media {
      MediaEpoch epoch;
      MediaRequest request;
      Timeline timeline;
      Readiness readiness;
      ReadyPurpose purpose = ReadyPurpose::None;
      std::optional<MediaTicks> target;
      std::optional<TimerId> readyDeadline;
      bool opened = false;
      bool started = false;
      bool paused = true;
      bool buffering = false;
      bool seeking = false;
      std::optional<MediaTicks> position;
      LocalInstant sampledAt;
      std::optional<MediaTicks> duration;
      std::optional<PlaybackRate> appliedRate;
      std::vector<ExternalTrackRequest> tracks;
    };

    struct Group {
      GroupEpoch epoch;
      GroupId id;
      std::string name;
      std::vector<std::string> participants;
      GroupState state = GroupState::Idle;
      PlaybackRate baseRate = PlaybackRate::normal();
      bool connected = true;
      std::optional<group::Command> lastCommand;
      std::optional<ServerInstant> lastQueueUpdate;
      PlaylistItemId playlistItem;
      bool syncEnabled = false;
      std::optional<LocalInstant> lastSyncAt;
      std::optional<PlaybackRate> correctionRate;
      SyncCorrection correction = SyncCorrection::None;
      std::uint32_t corrections = 0;
      std::optional<TimerId> commandTimer;
      std::optional<TimerId> cooldownTimer;
      std::optional<TimerId> correctionTimer;
      bool reportedBuffering = false;
      std::optional<std::int64_t> driftMilliseconds;
    };

    bool accept(const SessionInput& input);
    void handle(const SessionInput& input);

    void on(const intent::Open&);
    void on(const intent::Play&);
    void on(const intent::Pause&);
    void on(const intent::Seek&);
    void on(const intent::SetRate&);
    void on(const intent::Stop&);
    void on(const intent::Next&);
    void on(const intent::Previous&);
    void on(const intent::SelectTrack&);
    void on(const intent::AddExternalTrack&);
    void on(const intent::SetSyncCorrection&);
    void on(const backend::SourceOpened&);
    void on(const backend::SeekStarted&);
    void on(const backend::PlaybackRestarted&);
    void on(const backend::Progress&);
    void on(const backend::Ended&);
    void on(const backend::RequestFailed&);
    void on(const backend::TrackAdded&);
    void on(const group::Joined&);
    void on(const group::Left&);
    void on(const group::ParticipantsChanged&);
    void on(const group::ConnectionLost&);
    void on(const group::StateChanged&);
    void on(const group::Command&);
    void on(const group::QueueChanged&);
    void on(const group::RateChangeRejected&);
    void on(const group::ClockSynced&);
    void on(const TimerFired&);

    // Media helpers.
    void stopMedia(PlaybackPhase endPhase, std::string error = {});
    void applySample(const backend::Sample& sample);
    bool seekTo(MediaTicks target, ReadyPurpose purpose);
    void setPaused(bool paused);
    void applyEffectiveRate();
    PlaybackRate effectiveRate() const;
    void readyConfirmed(ReadyPurpose purpose);
    void issueExternalTracks();
    void stepQueue(bool forward);
    bool mediaFollowsGroup() const;
    bool mediaFollowsGroupItem(const PlaylistItemId& item) const;

    // Group helpers.
    void leaveGroup();
    void clearScheduledCommand();
    void clearCorrection();
    void cancelGroupTimers();
    void executeCommand(const group::Command& command, bool late);
    void applyDuplicate(const group::Command& command);
    void reportReady(bool ready);
    void synchronize();
    LocalInstant now() const { return m_ports.clock.now(); }
    ServerInstant serverNow() const { return m_offset.toServer(now()); }

    // Timers.
    TimerId arm(TimerPurpose purpose, LocalInstant due);
    void cancel(std::optional<TimerId>& timer);

    void publish();
    SessionSnapshot buildSnapshot() const;

    SessionPorts m_ports;
    SessionSettings m_settings;

    std::deque<SessionInput> m_queue;
    bool m_handling = false;
    std::uint64_t m_dropped = 0;

    EpochCounter<MediaEpochTag> m_mediaEpochs;
    EpochCounter<RequestTag> m_requests;
    GroupEpoch m_latestGroupEpoch;
    std::uint64_t m_timerSerial = 0;
    std::map<std::uint64_t, TimerId> m_armed;

    std::optional<Media> m_media;
    std::optional<Group> m_group;
    ClockOffset m_offset;
    PlaybackRate m_personalRate = PlaybackRate::normal();
    PlaybackPhase m_endPhase = PlaybackPhase::Idle;
    std::string m_error;

    SessionSnapshot m_snapshot;
  };

} // namespace jfd::core
