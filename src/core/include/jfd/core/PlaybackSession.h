#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/MediaBackend.h>
#include <jfd/core/Readiness.h>
#include <jfd/core/Reconciler.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/SessionJournal.h>
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
    // How long a correction seek (Sync) may take: after it drift correction
    // and the group's buffering report decide again.
    WallTicks syncReadyDeadline = std::chrono::seconds(5);
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

    // Records every input from now on and each SyncPlay message sent; the
    // journal must outlive the session or be unset first.
    void setJournal(SessionJournal* journal) { m_journal = journal; }

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
      // Opened by the automatic reopen after a stream error: no second one
      // until it has played on for a while.
      bool reopened = false;
      // A group seek that did not become ready was issued once more.
      bool seekRetried = false;
      // When the pending seek was issued, for the seek latency estimate.
      std::optional<LocalInstant> seekIssuedAt;
    };

    // A reopen asked for after a stream error, until its Open arrives.
    struct Reopen {
      ItemId item;
      PlaylistItemId playlistItem;
      std::string error;
    };

    // The file queued behind the current one (MediaBackend::preload).
    struct Preload {
      MediaEpoch epoch;
      MediaRequest request;
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
      // The queue goes on after the current item (group::QueueChanged).
      bool hasNextItem = false;
      // The item whose end already asked the group for the next one.
      PlaylistItemId nextRequestedFor;
      std::optional<std::int64_t> driftMilliseconds;
      // A command that came while the clock offset was unknown: scheduled
      // once ClockSynced arrives.
      bool commandDeferred = false;
      // A Ready (true) or Buffering (false) held back for the same reason.
      std::optional<bool> deferredReady;
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
    void on(const intent::SetAutoNext&);
    void on(const intent::OpenFailed&);
    void on(const backend::SourceOpened&);
    void on(const backend::SeekStarted&);
    void on(const backend::PlaybackRestarted&);
    void on(const backend::Progress&);
    void on(const backend::Ended&);
    void on(const backend::RequestFailed&);
    void on(const backend::TrackAdded&);
    void on(const backend::Advanced&);
    void on(const intent::PreloadNext&);
    void on(const group::Joined&);
    void on(const group::Left&);
    void on(const group::ParticipantsChanged&);
    void on(const group::ConnectionLost&);
    void on(const group::StateChanged&);
    void on(const group::Command&);
    void on(const group::QueueChanged&);
    void on(const group::RateChangeRejected&);
    void on(const group::ClockSynced&);
    void on(const group::ClockLost&);
    void on(const group::RequestFailed&);
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
    bool groupDrives() const;
    bool mediaFollowsGroup() const;
    bool mediaFollowsGroupItem(const PlaylistItemId& item) const;
    // After a stream error: asks the app to reopen the file where it was
    // (once), telling the group we wait for data. False when it may not.
    bool reopenAfterError(std::string error);
    // Stops the group's item for good and asks the app to leave the group.
    void failGroupItem(std::string error);
    // A group request while the connection is lost: tells the app, true.
    bool groupOffline();
    // How far ahead of the group a seek aims while it plays: the measured
    // time seeks take (zero until one was measured).
    WallTicks seekLead() const;

    // Group helpers.
    void leaveGroup();
    void clearScheduledCommand();
    void clearCorrection();
    void cancelGroupTimers();
    // Runs the last command now or arms its timer by server time.
    // `fresh`: just received (an Unpause far away seeks ahead of time).
    void scheduleCommand(bool fresh);
    void executeCommand(const group::Command& command, bool late);
    void applyDuplicate(const group::Command& command);
    void reportReady(bool ready);
    // Tells the group when our buffering state differs from what it was
    // last told (Buffering while we wait for data, Ready once we no longer
    // do). Called after every sample once no seek of ours is pending.
    void reportBufferingState();
    // After the paused seek of a group Pause while the group waits: Ready.
    void readyAfterGroupPause();
    void synchronize();
    LocalInstant now() const { return m_ports.clock.now(); }
    void send(const SyncPlayMessage& message);
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
    SessionJournal* m_journal = nullptr;

    EpochCounter<MediaEpochTag> m_mediaEpochs;
    std::optional<Preload> m_preload;
    EpochCounter<RequestTag> m_requests;
    GroupEpoch m_latestGroupEpoch;
    std::uint64_t m_timerSerial = 0;
    std::map<std::uint64_t, TimerId> m_armed;

    std::optional<Media> m_media;
    std::optional<Group> m_group;
    ClockOffset m_offset;
    // Whether m_offset was measured since the last ClockLost. A session
    // starts without one: server time is unknown until the first ClockSynced.
    bool m_clockSynced = false;
    // The round trip to the server of the latest ClockSynced, while synced.
    std::optional<WallTicks> m_roundTrip;
    PlaybackRate m_personalRate = PlaybackRate::normal();
    bool m_autoNext = true;
    std::optional<Reopen> m_reopen;
    // Moving average of the time from a seek to its readiness.
    std::optional<WallTicks> m_seekLatency;
    PlaybackPhase m_endPhase = PlaybackPhase::Idle;
    std::string m_error;

    SessionSnapshot m_snapshot;
  };

} // namespace jfd::core
