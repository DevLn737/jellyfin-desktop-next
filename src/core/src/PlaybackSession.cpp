#include <jfd/core/PlaybackSession.h>

// JFD_CORE_MUTATION selects a deliberately injected bug for the simulator's
// mutation check (tests/CMakeLists.txt). It is never set in a normal build.
#ifndef JFD_CORE_MUTATION
#define JFD_CORE_MUTATION 0
#endif

#include <type_traits>
#include <utility>

namespace jfd::core {

  namespace {
    template <class... Ts> struct Overloaded : Ts... {
      using Ts::operator()...;
    };
    template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

    bool changesCurrentItem(QueueChangeReason reason) {
      switch (reason) {
      case QueueChangeReason::NewPlaylist:
      case QueueChangeReason::SetCurrentItem:
      case QueueChangeReason::NextItem:
      case QueueChangeReason::PreviousItem:
      case QueueChangeReason::RemoveItems:
        return true;
      default:
        return false;
      }
    }

    bool sameCommand(const group::Command& a, const group::Command& b) {
      return a.type == b.type && a.when == b.when && a.position == b.position &&
             a.playlistItem == b.playlistItem && a.baseRate == b.baseRate;
    }
  } // namespace

  PlaybackSession::PlaybackSession(SessionPorts ports, SessionSettings settings)
      : m_ports(ports), m_settings(settings) {
    m_snapshot = buildSnapshot();
  }

  void PlaybackSession::post(SessionInput input) {
    m_queue.push_back(std::move(input));
    if (m_handling) return;
    m_handling = true;
    while (!m_queue.empty()) {
      SessionInput next = std::move(m_queue.front());
      m_queue.pop_front();
      const bool accepted = accept(next);
      if (m_journal) m_journal->input(now(), next, accepted);
      if (accepted) {
        handle(next);
      } else {
        ++m_dropped;
      }
      publish();
    }
    m_handling = false;
  }

  // The one place where stale inputs are recognised.
  bool PlaybackSession::accept(const SessionInput& input) {
    return std::visit(
        Overloaded{
            [&](const intent::Open&) { return true; },
            [&](const intent::Play&) { return true; },
            [&](const intent::Pause&) { return true; },
            [&](const intent::Seek&) { return true; },
            [&](const intent::SetRate&) { return true; },
            [&](const intent::Stop&) { return true; },
            [&](const intent::Next&) { return true; },
            [&](const intent::Previous&) { return true; },
            [&](const intent::SetSyncCorrection&) { return true; },
            [&](const intent::SetAutoNext&) { return true; },
            [&](const intent::OpenFailed&) { return true; },
            [&](const intent::PreloadNext&) { return true; },
            [&](const backend::Advanced& advanced) {
              return m_media.has_value() && m_media->epoch == advanced.from && m_preload &&
                     m_preload->epoch == advanced.to;
            },
            [&](const group::Joined& joined) {
              return joined.epoch.isValid() && joined.epoch > m_latestGroupEpoch;
            },
            [&](const group::ClockSynced&) { return true; },
            [&](const group::ClockLost&) { return true; },
            // The epoch says which membership failed; on() checks it.
            [&](const group::RequestFailed&) { return true; },
            [&](const TimerFired& fired) {
              const auto armed = m_armed.find(fired.id.serial);
              if (armed == m_armed.end() || armed->second != fired.id) return false;
              m_armed.erase(armed);
#if JFD_CORE_MUTATION == 3
              return true; // Mutation: timers outlive their group.
#endif
              if (fired.id.group.isValid() && (!m_group || m_group->epoch != fired.id.group))
                return false;
              return true;
            },
            [&](const auto& event) {
              using EpochType = std::remove_cvref_t<decltype(event.epoch)>;
              if constexpr (std::is_same_v<EpochType, MediaEpoch>) {
#if JFD_CORE_MUTATION == 1
                return m_media.has_value(); // Mutation: epoch check disabled.
#endif
                return m_media.has_value() && m_media->epoch == event.epoch;
              } else {
                static_assert(std::is_same_v<EpochType, GroupEpoch>);
                return m_group.has_value() && m_group->epoch == event.epoch;
              }
            },
        },
        input);
  }

  void PlaybackSession::handle(const SessionInput& input) {
    std::visit([&](const auto& value) { on(value); }, input);
  }

  void PlaybackSession::send(const SyncPlayMessage& message) {
    if (m_journal) m_journal->output(now(), message);
    m_ports.syncPlay.send(message);
  }

  // ---- Intents -------------------------------------------------------------

  void PlaybackSession::on(const intent::Open& open) {
    if (m_media) m_ports.backend.stop(m_media->epoch);
    m_preload.reset();
    // The reopen after an error, or anything else the app opened instead.
    const bool reopened = m_reopen && m_reopen->item == open.media.item;
    m_reopen.reset();
    Media media;
    media.epoch = m_mediaEpochs.advance();
    media.request = open.media;
    media.reopened = reopened;
    media.timeline = Timeline(open.media.timeline, open.media.start);
    media.duration = open.media.runTime;
    media.paused = true;
    const bool forGroup = mediaFollowsGroupItem(open.media.playlistItem);
    media.purpose = forGroup ? ReadyPurpose::GroupOpen : ReadyPurpose::Local;
    m_media = std::move(media);
    m_endPhase = PlaybackPhase::Idle;
    m_error.clear();
    if (m_group) m_group->reportedBuffering = false;

    BackendLoad load;
    load.url = open.media.url;
    load.rebaseStartTime = m_media->timeline.rebasesStartTime();
    // A group member waits paused until the group unpauses.
    load.startPaused = forGroup || open.media.startPaused;
    m_media->paused = load.startPaused;
    m_media->readyDeadline = arm(TimerPurpose::ReadyDeadline, now() + m_settings.readyDeadline);
    m_ports.backend.load(m_media->epoch, load);
  }

  void PlaybackSession::on(const intent::PreloadNext& next) {
    // Group items come from the server's queue; nothing is playing to follow.
    if (!m_media || m_group) return;
    Preload preload;
    preload.epoch = m_mediaEpochs.advance();
    preload.request = next.media;
    BackendLoad load;
    load.url = next.media.url;
    load.rebaseStartTime = Timeline(next.media.timeline, next.media.start).rebasesStartTime();
    m_preload = std::move(preload);
    m_ports.backend.preload(m_preload->epoch, load);
  }

  void PlaybackSession::on(const intent::Play&) {
    if (groupDrives()) {
      if (groupOffline()) return;
      send(outgoing::RequestUnpause{m_group->epoch});
      return;
    }
    setPaused(false);
  }

  void PlaybackSession::on(const intent::Pause&) {
    if (groupDrives()) {
      if (groupOffline()) return;
      send(outgoing::RequestPause{m_group->epoch});
      return;
    }
    setPaused(true);
  }

  void PlaybackSession::on(const intent::Seek& seek) {
    if (groupDrives()) {
      if (groupOffline()) return;
      send(outgoing::RequestSeek{m_group->epoch, seek.position});
      return;
    }
    seekTo(seek.position, ReadyPurpose::Local);
  }

  void PlaybackSession::on(const intent::SetRate& rate) {
    if (groupDrives()) {
      if (groupOffline()) return;
      // The group rate is a base rate; the server validates the same range.
      if (const auto base = PlaybackRate::basePerMille(rate.rate.perMille()))
        send(outgoing::RequestPlaybackRate{m_group->epoch, *base});
      return;
    }
    if (!PlaybackRate::basePerMille(rate.rate.perMille())) return;
    m_personalRate = rate.rate;
    applyEffectiveRate();
  }

  void PlaybackSession::on(const intent::Stop&) {
    // A reopen still on its way is not wanted any more.
    if (m_reopen && !m_media) m_endPhase = PlaybackPhase::Idle;
    m_reopen.reset();
    if (groupDrives()) {
      if (groupOffline()) return;
      send(outgoing::RequestStop{m_group->epoch});
      return;
    }
    stopMedia(PlaybackPhase::Idle);
  }

  void PlaybackSession::on(const intent::Next&) { stepQueue(true); }

  void PlaybackSession::on(const intent::Previous&) { stepQueue(false); }

  void PlaybackSession::on(const intent::SetSyncCorrection& set) { m_settings.reconciler = set.reconciler; }

  void PlaybackSession::on(const intent::SetAutoNext& set) { m_autoNext = set.enabled; }

  void PlaybackSession::on(const intent::OpenFailed& failed) {
    // Only a reopen this session asked for is its business; a group item
    // the app could not resolve is left by the app itself.
    if (!m_reopen || m_reopen->item != failed.item || m_media) return;
    m_endPhase = PlaybackPhase::Failed;
    m_error = std::move(m_reopen->error);
    m_reopen.reset();
  }

  void PlaybackSession::stepQueue(bool forward) {
    if (!m_group) {
      m_ports.observer.notify(notice::QueueStepRequested{forward});
      return;
    }
    if (m_group->playlistItem.isEmpty()) return;
    if (groupOffline()) return;
    if (forward)
      send(outgoing::RequestNextItem{m_group->epoch, m_group->playlistItem});
    else
      send(outgoing::RequestPreviousItem{m_group->epoch, m_group->playlistItem});
  }

  // A track switch is local: it changes neither the position nor readiness,
  // and the group is not told. mpv may still restart playback or wait for
  // data after it; those arrive as ordinary backend events.
  void PlaybackSession::on(const intent::SelectTrack& select) {
    m_ports.backend.selectTrack(m_media->epoch, select.kind, select.trackId);
  }

  void PlaybackSession::on(const intent::AddExternalTrack& add) {
    ExternalTrackRequest track;
    track.request = m_requests.advance();
    track.state.track = add.track;
    m_media->tracks.push_back(std::move(track));
    // Before the file is open the backend has nothing to add a track to.
    if (m_media->opened) issueExternalTracks();
  }

  void PlaybackSession::issueExternalTracks() {
    for (auto& track : m_media->tracks) {
      if (track.issued) continue;
      track.issued = true;
      m_ports.backend.addExternalTrack(m_media->epoch, track.request, track.state.track);
    }
  }

  // ---- Backend events ------------------------------------------------------

  void PlaybackSession::on(const backend::SourceOpened& opened) {
    Media& media = *m_media;
    if (media.opened) return;
    if (!media.timeline.configure(opened.demuxerStart)) {
      stopMedia(PlaybackPhase::Failed, "The stream did not report its start time.");
      return;
    }
    media.opened = true;
    if (!media.duration && opened.duration) media.duration = media.timeline.duration(*opened.duration);
    applyEffectiveRate();
    const auto start = media.timeline.startPosition();
    std::optional<MediaTicks> target;
    if (start) target = media.request.start;
    media.target = target;
    media.readiness.begin(m_requests.advance(), target, false);
    m_ports.backend.start(media.epoch, start);
    issueExternalTracks();
  }

  void PlaybackSession::on(const backend::SeekStarted&) {
    m_media->readiness.seekStarted();
    m_media->seeking = true;
  }

  void PlaybackSession::on(const backend::PlaybackRestarted& restarted) {
    applySample(restarted.sample);
    Media& media = *m_media;
    const auto completed =
        media.readiness.restarted(media.position, restarted.sample.seeking, media.paused);
    if (completed) {
      const ReadyPurpose purpose = media.purpose;
      media.purpose = ReadyPurpose::None;
      media.target.reset();
      media.seekRetried = false;
      cancel(media.readyDeadline);
      media.started = true;
      if (media.seekIssuedAt) {
        // Clamped, so one stall does not make every later seek aim far ahead.
        const WallTicks took = std::min<WallTicks>(now() - *media.seekIssuedAt, std::chrono::seconds(3));
        m_seekLatency = m_seekLatency ? (*m_seekLatency * 3 + took) / 4 : took;
        media.seekIssuedAt.reset();
      }
      readyConfirmed(purpose);
    }
    // Pause, seeking and buffering changes that came with the restart are
    // in this sample only: no Progress may follow while paused.
    if (m_media && m_group && mediaFollowsGroup() && m_media->started) reportBufferingState();
  }

  void PlaybackSession::on(const backend::Progress& progress) {
    applySample(progress.sample);
    if (!m_group || !mediaFollowsGroup() || !m_media->started) return;
    reportBufferingState();
    synchronize();
  }

  void PlaybackSession::reportBufferingState() {
    // A seek of ours reports (or not) when it is ready; until then the
    // backend's buffering belongs to the seek.
    if (m_media->purpose != ReadyPurpose::None) return;
    if (m_media->buffering && !m_group->reportedBuffering) {
      // Leaving any correction while the backend waits for data, as Web does.
      clearCorrection();
      reportReady(false);
    } else if (!m_media->buffering && m_group->reportedBuffering) {
      // Buffering ended, perhaps during a seek (the group's Pause answers
      // our Buffering with one): the group waits for our Ready until we
      // send it.
      reportReady(true);
    }
  }

  void PlaybackSession::on(const backend::Ended& ended) {
    switch (ended.reason) {
    case backend::EndReason::EndOfFile: {
      // Nobody hosts a group: every member that reaches the end asks for the
      // next item, as Jellyfin Web does. The server moves on for the first
      // and ignores the rest (they name an item that is no longer current).
      const bool askNext = m_autoNext && mediaFollowsGroup() && m_group->connected &&
                           m_group->hasNextItem &&
                           m_group->nextRequestedFor != m_media->request.playlistItem;
      const PlaylistItemId item = m_media->request.playlistItem;
      stopMedia(PlaybackPhase::Ended);
      if (askNext) {
        m_group->nextRequestedFor = item;
        send(outgoing::RequestNextItem{m_group->epoch, item});
      }
      break;
    }
    case backend::EndReason::Stopped:
      stopMedia(PlaybackPhase::Idle);
      break;
    case backend::EndReason::Error: {
      std::string error = ended.error.empty() ? "Playback failed." : ended.error;
      if (reopenAfterError(error)) break;
      if (mediaFollowsGroup()) {
        failGroupItem(std::move(error));
        break;
      }
      stopMedia(PlaybackPhase::Failed, std::move(error));
      break;
    }
    }
  }

  bool PlaybackSession::reopenAfterError(std::string error) {
    Media& media = *m_media;
    // Only a file that played: one that never opened will not open now
    // either. A live stream (no run time) has no position to return to.
    if (media.reopened || !media.started || !media.position || !media.request.runTime ||
        media.request.audioOnly)
      return false;
    // The group moved on to another item, which the app is opening: a
    // reopen of the old one would replace it.
    if (m_group && !media.request.playlistItem.isEmpty() && !mediaFollowsGroup()) return false;
    notice::ReopenAfterError reopen;
    reopen.item = media.request.item;
    reopen.mediaSource = media.request.mediaSource;
    reopen.playlistItem = media.request.playlistItem;
    reopen.position = *media.position;
    reopen.startPaused = media.paused;
    if (mediaFollowsGroup()) {
      // The group waits for us as for any member that waits for data: the
      // reopened file reports Ready like a first load (GroupOpen).
      if (!m_group->reportedBuffering && media.purpose == ReadyPurpose::None) {
        clearCorrection();
        reportReady(false);
      }
      reopen.startPaused = true;
    }
    stopMedia(PlaybackPhase::Loading);
    m_reopen = Reopen{reopen.item, reopen.playlistItem, std::move(error)};
    m_ports.observer.notify(reopen);
    return true;
  }

  void PlaybackSession::failGroupItem(std::string error) {
    stopMedia(PlaybackPhase::Failed, std::move(error));
    m_ports.observer.notify(notice::GroupItemFailed{});
  }

  bool PlaybackSession::groupOffline() {
    if (m_group->connected) return false;
    m_ports.observer.notify(notice::GroupOffline{});
    return true;
  }

  WallTicks PlaybackSession::seekLead() const { return m_seekLatency.value_or(WallTicks(0)); }

  void PlaybackSession::on(const backend::RequestFailed& failed) {
    Media& media = *m_media;
    for (auto& track : media.tracks) {
      if (track.request != failed.request) continue;
      track.state.status = ExternalTrackState::Status::Failed;
      track.state.error = failed.error.empty() ? "The track could not be added." : failed.error;
      return;
    }
    if (media.readiness.pending() != failed.request) return;
    media.readiness.failed(failed.request);
    media.purpose = ReadyPurpose::None;
    media.target.reset();
    cancel(media.readyDeadline);
  }

  void PlaybackSession::on(const backend::Advanced&) {
    // The backend went on to the queued file by itself: it is already playing
    // from its beginning, so only the session's view of the media changes. Its
    // SourceOpened, restart and progress follow like those of any file.
    const bool paused = m_media->paused;
    cancel(m_media->readyDeadline);
    Media media;
    media.epoch = m_preload->epoch;
    media.request = std::move(m_preload->request);
    media.timeline = Timeline(media.request.timeline, media.request.start);
    media.duration = media.request.runTime;
    media.paused = paused;
    media.purpose = ReadyPurpose::Local;
    m_preload.reset();
    m_media = std::move(media);
    m_endPhase = PlaybackPhase::Idle;
    m_error.clear();
    m_media->readyDeadline = arm(TimerPurpose::ReadyDeadline, now() + m_settings.readyDeadline);
  }

  void PlaybackSession::on(const backend::TrackAdded& added) {
    for (auto& track : m_media->tracks) {
      if (track.request != added.request) continue;
      track.state.status = ExternalTrackState::Status::Added;
      track.state.trackId = added.trackId;
      return;
    }
  }

  // ---- Group messages ------------------------------------------------------

  void PlaybackSession::on(const group::Joined& joined) {
    if (m_group) cancelGroupTimers();
    m_latestGroupEpoch = joined.epoch;
    Group group;
    group.epoch = joined.epoch;
    group.id = joined.group;
    group.name = joined.name;
    group.participants = joined.participants;
    m_group = std::move(group);
    applyEffectiveRate();
  }

  void PlaybackSession::on(const group::Left&) { leaveGroup(); }

  void PlaybackSession::on(const group::ParticipantsChanged& changed) {
    m_group->participants = changed.participants;
  }

  void PlaybackSession::on(const group::ConnectionLost&) {
    clearScheduledCommand();
    m_group->connected = false;
    m_group->lastCommand.reset();
    m_group->driftMilliseconds.reset();
  }

  void PlaybackSession::on(const group::StateChanged& changed) { m_group->state = changed.state; }

  void PlaybackSession::on(const group::QueueChanged& queue) {
    Group& group = *m_group;
    if (group.lastQueueUpdate && queue.lastUpdate <= *group.lastQueueUpdate) {
      ++m_dropped;
      return;
    }
    group.lastQueueUpdate = queue.lastUpdate;
    group.baseRate = queue.baseRate;
    group.hasNextItem = queue.hasNextItem;
    applyEffectiveRate();
    if (!changesCurrentItem(queue.reason)) return;
    const bool itemChanged = group.playlistItem != queue.currentPlaylistItem;
    group.playlistItem = queue.currentPlaylistItem;
    if (group.playlistItem.isEmpty()) return;

    if (m_media && m_media->request.playlistItem == group.playlistItem) {
      // Rejoining (or a queue edit) with this occurrence already open: report
      // where we are instead of reloading, now or once a pending seek is done.
      if (!m_media->started) {
        m_media->purpose = ReadyPurpose::GroupOpen;
        // Its own deadline may have passed while it was a file of our own.
        if (!m_media->readyDeadline)
          m_media->readyDeadline = arm(TimerPurpose::ReadyDeadline, now() + m_settings.readyDeadline);
      } else if (m_media->purpose == ReadyPurpose::None)
        reportReady(true);
      else if (m_media->purpose != ReadyPurpose::GroupOpen)
        m_media->purpose = ReadyPurpose::GroupSeek;
      return;
    }
    if (itemChanged || queue.reason == QueueChangeReason::NewPlaylist)
      m_ports.observer.notify(notice::GroupItemRequested{queue.currentPlaylistItem,
                                                          queue.currentItem, queue.startPosition});
  }

  void PlaybackSession::on(const group::RateChangeRejected& rejected) {
    m_ports.observer.notify(
        notice::GroupRateRejected{rejected.httpStatus, rejected.unsupportedParticipants});
  }

  void PlaybackSession::on(const group::ClockSynced& synced) {
    const bool changed = !m_clockSynced || synced.offset != m_offset;
    m_offset = synced.offset;
    m_clockSynced = true;
    m_roundTrip = synced.roundTrip;
    if (!m_group || !changed) return;
    // What waited for server time, or was armed by the old offset.
    if (m_group->deferredReady) {
      const bool ready = *m_group->deferredReady;
      m_group->deferredReady.reset();
      reportReady(ready);
    }
    if (m_group->commandDeferred) {
      m_group->commandDeferred = false;
      scheduleCommand(true);
    } else if (m_group->commandTimer) {
      cancel(m_group->commandTimer);
      scheduleCommand(false);
    }
  }

  void PlaybackSession::on(const group::ClockLost&) {
    m_clockSynced = false;
    m_roundTrip.reset();
    if (!m_group) return;
    // An armed command would fire by a clock that may be wrong by however
    // long the computer slept: wait for the new offset.
    if (m_group->commandTimer) {
      cancel(m_group->commandTimer);
      m_group->commandDeferred = true;
    }
  }

  void PlaybackSession::on(const group::RequestFailed& failed) {
    switch (failed.request) {
    case group::Request::Create:
    case group::Request::Join:
      break;
    case group::Request::Rejoin:
    case group::Request::Leave:
    case group::Request::Ready:
    case group::Request::Buffering:
      if (!m_group || m_group->epoch != failed.epoch) return;
      break;
    }
    m_ports.observer.notify(notice::GroupRequestFailed{failed.request, failed.httpStatus});
  }

  void PlaybackSession::on(const group::Command& command) {
    Group& group = *m_group;
    group.connected = true;
    group.baseRate = command.baseRate;

    if (group.lastCommand && sameCommand(*group.lastCommand, command)) {
      applyDuplicate(command);
      return;
    }
    group.lastCommand = command;
    clearScheduledCommand();
    if (!mediaFollowsGroup() || !m_media->started) {
      // Still loading this occurrence: the server waits for our Ready and
      // repeats the command once everyone is ready.
      // Not another file the user opened on their own while in the group.
      if (command.type == GroupCommandType::Stop && m_media && mediaFollowsGroup())
        stopMedia(PlaybackPhase::Idle);
      // The group stopped while our reopen is on its way: nothing to wait for.
      if (command.type == GroupCommandType::Stop && !m_media && m_reopen &&
          m_reopen->playlistItem == group.playlistItem) {
        m_reopen.reset();
        m_endPhase = PlaybackPhase::Idle;
      }
      return;
    }

    if (!m_clockSynced && command.type != GroupCommandType::Stop) {
      // When is server time; without an offset it cannot be placed.
      group.commandDeferred = true;
      return;
    }
    scheduleCommand(true);
  }

  void PlaybackSession::scheduleCommand(bool fresh) {
    Group& group = *m_group;
    if (!group.lastCommand || !mediaFollowsGroup() || !m_media->started) return;
    const group::Command& command = *group.lastCommand;
    const LocalInstant due = m_offset.toLocal(command.when);
    if (due > now()) {
      if (fresh && command.type == GroupCommandType::Unpause && m_media->position &&
          (*m_media->position - command.position).abs() > m_settings.reconciler.skipMinimum)
        seekTo(command.position, ReadyPurpose::Sync);
      group.commandTimer = arm(TimerPurpose::GroupCommand, due);
      return;
    }
    executeCommand(command, true);
  }

  // ---- Timers --------------------------------------------------------------

  void PlaybackSession::on(const TimerFired& fired) {
    switch (fired.id.purpose) {
    case TimerPurpose::GroupCommand:
      m_group->commandTimer.reset();
      if (m_group->lastCommand && mediaFollowsGroup() && m_media->started)
        executeCommand(*m_group->lastCommand, false);
      break;
    case TimerPurpose::CorrectionCooldown:
      m_group->cooldownTimer.reset();
      m_group->syncEnabled = true;
      m_group->correction = SyncCorrection::None;
      break;
    case TimerPurpose::CorrectionEnd:
#if JFD_CORE_MUTATION == 3
      if (!m_group) {
        // Mutation: a late correction timer restores the group base rate.
        if (m_media) m_ports.backend.setRate(m_media->epoch, PlaybackRate::normal());
        break;
      }
#endif
      m_group->correctionTimer.reset();
      m_group->correctionRate.reset();
      m_group->correction = SyncCorrection::None;
      m_group->syncEnabled = true;
      applyEffectiveRate();
      break;
    case TimerPurpose::ReadyDeadline: {
      if (!m_media || m_media->readyDeadline != fired.id) break;
      m_media->readyDeadline.reset();
      const ReadyPurpose purpose = m_media->purpose;
      if (purpose == ReadyPurpose::Sync) {
        // A correction seek whose restart never came (mpv merged or dropped
        // it): stop waiting, so drift correction and the buffering report
        // go on. A late restart still completes it harmlessly.
        m_media->purpose = ReadyPurpose::None;
        m_media->target.reset();
        m_media->seekIssuedAt.reset();
        if (m_group && mediaFollowsGroup() && m_media->started) {
          // Each sends Ready only when the other does not.
          readyAfterGroupPause();
          reportBufferingState();
        }
        break;
      }
      m_ports.observer.notify(notice::ReadyTimedOut{m_media->target.value_or(MediaTicks{})});
      if (purpose == ReadyPurpose::GroupSeek && m_media->target && !m_media->seekRetried) {
        // As Jellyfin Web does after its ready timeout: seek again, once.
        m_media->seekRetried = true;
        seekTo(*m_media->target, ReadyPurpose::GroupSeek);
      } else if ((purpose == ReadyPurpose::GroupOpen || purpose == ReadyPurpose::GroupSeek) &&
                 mediaFollowsGroup()) {
        // The group would wait for our Ready forever.
        failGroupItem("The file did not become ready in time.");
      }
      break;
    }
    }
  }

  // ---- Helpers -------------------------------------------------------------

  bool PlaybackSession::mediaFollowsGroupItem(const PlaylistItemId& item) const {
    return m_group && !item.isEmpty() && item == m_group->playlistItem;
  }

  bool PlaybackSession::groupDrives() const {
    // A file opened on one's own while in a group (nothing queued there, or
    // music) is played and controlled locally; the group's commands, its
    // speed and its pauses are not for it.
    return m_group && (!m_media || mediaFollowsGroup());
  }

  bool PlaybackSession::mediaFollowsGroup() const {
    return m_media && mediaFollowsGroupItem(m_media->request.playlistItem);
  }

  void PlaybackSession::stopMedia(PlaybackPhase endPhase, std::string error) {
    m_preload.reset();
    if (m_media) {
      cancel(m_media->readyDeadline);
      m_ports.backend.stop(m_media->epoch);
      m_media.reset();
    }
    if (m_group) {
      clearScheduledCommand();
      m_group->reportedBuffering = false;
      m_group->driftMilliseconds.reset();
    }
    m_endPhase = endPhase;
    m_error = std::move(error);
  }

  void PlaybackSession::applySample(const backend::Sample& sample) {
    Media& media = *m_media;
    media.paused = sample.paused;
    media.buffering = sample.buffering;
    media.seeking = sample.seeking;
    if (sample.position) {
      if (const auto position = media.timeline.toMedia(*sample.position)) {
        media.position = position;
        media.sampledAt = sample.sampledAt;
        // A reopened file that played on for a minute may be reopened again.
        if (media.reopened && media.started &&
            *position - media.request.start >= MediaDelta::fromMilliseconds(60'000))
          media.reopened = false;
      }
    }
  }

  bool PlaybackSession::seekTo(MediaTicks target, ReadyPurpose purpose) {
    if (!m_media || !m_media->opened) return false;
    Media& media = *m_media;
    const auto backendTarget = media.timeline.toBackend(target);
    if (!backendTarget) {
      m_ports.observer.notify(notice::StreamReopenNeeded{target});
      return false;
    }
    const RequestId request = m_requests.advance();
    media.readiness.begin(request, target, true);
    media.purpose = purpose;
    media.target = target;
    media.seekIssuedAt = now();
    cancel(media.readyDeadline);
    if (purpose == ReadyPurpose::GroupSeek || purpose == ReadyPurpose::Local)
      media.readyDeadline = arm(TimerPurpose::ReadyDeadline, now() + m_settings.readyDeadline);
    else if (purpose == ReadyPurpose::Sync)
      media.readyDeadline = arm(TimerPurpose::ReadyDeadline, now() + m_settings.syncReadyDeadline);
    m_ports.backend.seek(media.epoch, request, *backendTarget);
    return true;
  }

  void PlaybackSession::setPaused(bool paused) {
    if (!m_media) return;
    m_ports.backend.setPaused(m_media->epoch, paused);
    // The backend confirms through a sample; until then the request stands.
    m_media->paused = paused;
  }

  PlaybackRate PlaybackSession::effectiveRate() const {
    if (!groupDrives()) return m_personalRate;
    return m_group->correctionRate.value_or(m_group->baseRate);
  }

  void PlaybackSession::applyEffectiveRate() {
    if (!m_media || !m_media->opened) return;
    const PlaybackRate rate = effectiveRate();
    if (m_media->appliedRate == rate) return;
    m_media->appliedRate = rate;
    m_ports.backend.setRate(m_media->epoch, rate);
  }

  void PlaybackSession::readyConfirmed(ReadyPurpose purpose) {
    switch (purpose) {
    case ReadyPurpose::GroupOpen:
      // Wait paused for the group; report where we really are.
      if (!m_media->paused) setPaused(true);
      if (mediaFollowsGroup()) reportReady(true);
      break;
    case ReadyPurpose::GroupSeek:
      if (mediaFollowsGroup()) reportReady(true);
      break;
    case ReadyPurpose::Sync:
      readyAfterGroupPause();
      break;
    case ReadyPurpose::None:
    case ReadyPurpose::Local:
      break;
    }
  }

  void PlaybackSession::readyAfterGroupPause() {
    // The group's Pause while it waits (for us, or for another member): the
    // server may have marked us not ready (our Buffering and Ready can reach
    // it in either order), so the paused seek ends with a Ready. The server
    // takes a Ready from a member it does not wait for as a no-op.
    if (!m_group || !mediaFollowsGroup() || !m_media->paused || m_media->buffering) return;
    if (m_group->state != GroupState::Waiting || !m_group->lastCommand ||
        m_group->lastCommand->type != GroupCommandType::Pause)
      return;
    if (m_group->reportedBuffering) return; // reportBufferingState sends it.
    reportReady(true);
  }

  void PlaybackSession::reportReady(bool ready) {
    if (!m_group || !m_media || !m_media->position) return;
    if (!m_clockSynced) {
      // When is server time: report once the offset is known.
      m_group->deferredReady = ready;
      return;
    }
    m_group->deferredReady.reset();
    const LocalInstant current = now();
    const ServerInstant when = m_offset.toServer(current);
    const bool playing = !m_media->paused && !m_media->buffering;
    MediaTicks position = *m_media->position;
    // A playing sample is already old by the time it is reported; move it to
    // the report's instant so When and PositionTicks describe the same moment.
    if (playing && current > m_media->sampledAt)
      position = advance(position, mediaElapsed(current - m_media->sampledAt, effectiveRate()))
                     .value_or(position);
    if (ready) {
      m_group->reportedBuffering = false;
      send(outgoing::Ready{m_group->epoch, when, position, playing,
                                            m_media->request.playlistItem});
    } else {
      m_group->reportedBuffering = true;
      send(outgoing::Buffering{m_group->epoch, when, position, !m_media->paused,
                                                m_media->request.playlistItem});
    }
  }

  void PlaybackSession::leaveGroup() {
    if (!m_group) return;
    // Cancel group work before restoring the personal rate, so no late
    // correction can overwrite it.
#if JFD_CORE_MUTATION != 3
    cancelGroupTimers();
#endif
    m_group.reset();
    if (m_media && m_media->purpose == ReadyPurpose::GroupOpen) m_media->purpose = ReadyPurpose::Local;
    if (m_media && m_media->purpose == ReadyPurpose::GroupSeek) m_media->purpose = ReadyPurpose::Local;
    applyEffectiveRate();
  }

  void PlaybackSession::clearCorrection() {
    if (!m_group) return;
    cancel(m_group->correctionTimer);
    m_group->correctionRate.reset();
    m_group->correction = SyncCorrection::None;
    applyEffectiveRate();
  }

  void PlaybackSession::clearScheduledCommand() {
    if (!m_group) return;
    m_group->commandDeferred = false;
    cancel(m_group->commandTimer);
    cancel(m_group->cooldownTimer);
    m_group->syncEnabled = false;
    clearCorrection();
  }

  void PlaybackSession::cancelGroupTimers() {
    if (!m_group) return;
    cancel(m_group->commandTimer);
    cancel(m_group->cooldownTimer);
    cancel(m_group->correctionTimer);
    m_group->correctionRate.reset();
    m_group->correction = SyncCorrection::None;
    m_group->syncEnabled = false;
  }

  void PlaybackSession::executeCommand(const group::Command& command, bool late) {
    switch (command.type) {
    case GroupCommandType::Unpause: {
      if (late) {
        // The group already started: join at its current position, unless we
        // are already close enough for speed correction to cover the rest.
        const GroupTimeline timeline{command.position, command.when, command.baseRate};
        const auto here = groupPosition(timeline, serverNow());
        if (here && (!m_media->position ||
                     (*here - *m_media->position).abs() >= m_settings.reconciler.speedMinimum)) {
          // The group moves on while the seek runs: aim where it will be.
          const auto target = groupPosition(timeline, serverNow() + seekLead());
          seekTo(target.value_or(*here), ReadyPurpose::Sync);
        }
      }
      setPaused(false);
      m_group->cooldownTimer =
          arm(TimerPurpose::CorrectionCooldown, now() + m_settings.correctionCooldown);
      break;
    }
    case GroupCommandType::Pause: {
      setPaused(true);
      // A group seek still on its way owes the group a Ready (the server
      // pauses everyone when a member joins, also while others seek): the
      // new seek keeps that purpose so the Ready is still sent.
      const bool owesReady = m_media->purpose == ReadyPurpose::GroupSeek;
      seekTo(command.position, owesReady ? ReadyPurpose::GroupSeek : ReadyPurpose::Sync);
      break;
    }
    case GroupCommandType::Seek:
      setPaused(true);
      seekTo(command.position, ReadyPurpose::GroupSeek);
      break;
    case GroupCommandType::Stop:
      stopMedia(PlaybackPhase::Idle);
      break;
    }
  }

  void PlaybackSession::applyDuplicate(const group::Command& command) {
    // Already scheduled, or waiting for the clock: nothing to do.
    if (!m_clockSynced || m_group->commandDeferred) return;
    if (m_offset.toLocal(command.when) > now()) return;
    if (!mediaFollowsGroup() || !m_media->started) return;
    const bool offTarget =
        !m_media->position || (*m_media->position - command.position).abs() > ReadyTolerance;
    switch (command.type) {
    case GroupCommandType::Unpause:
      if (m_media->paused) {
        clearScheduledCommand();
        executeCommand(command, true);
      }
      break;
    case GroupCommandType::Pause:
      if (!m_media->paused || offTarget) {
        clearScheduledCommand();
        executeCommand(command, false);
      }
      break;
    case GroupCommandType::Seek:
      if (!m_media->paused || offTarget) {
        clearScheduledCommand();
        executeCommand(command, false);
      } else if (m_media->purpose == ReadyPurpose::None) {
        reportReady(true);
      }
      break;
    case GroupCommandType::Stop:
      stopMedia(PlaybackPhase::Idle);
      break;
    }
  }

  void PlaybackSession::synchronize() {
    Group& group = *m_group;
    Media& media = *m_media;
    group.driftMilliseconds.reset();
    if (!group.connected || !group.lastCommand ||
        group.lastCommand->type != GroupCommandType::Unpause ||
        group.lastCommand->playlistItem != media.request.playlistItem)
      return;
    if (media.buffering || media.paused || media.seeking || !media.position ||
        media.purpose != ReadyPurpose::None || !m_clockSynced)
      return;

    const GroupTimeline timeline{group.lastCommand->position, group.lastCommand->when,
                                 group.baseRate};
    ReconcilerSettings settings = m_settings.reconciler;
    settings.enabled = settings.enabled && group.syncEnabled;
    const auto decision =
        reconcile(timeline, m_offset.toServer(media.sampledAt), *media.position, settings);
    group.driftMilliseconds = decision.drift.milliseconds();
    if (!group.syncEnabled) return;

    const LocalInstant current = now();
    // Throttled as in Jellyfin Web: at most one decision per cooldown.
    if (group.lastSyncAt && current - *group.lastSyncAt < m_settings.correctionCooldown) return;
    group.lastSyncAt = current;

    std::visit(Overloaded{
                   [&](const drift::InSync&) { group.corrections = 0; },
                   [&](const drift::SpeedCorrection& correction) {
                     group.syncEnabled = false;
                     ++group.corrections;
                     group.correction = SyncCorrection::Speed;
                     group.correctionRate = correction.rate;
                     applyEffectiveRate();
                     group.correctionTimer =
                         arm(TimerPurpose::CorrectionEnd, current + correction.duration);
                   },
                   [&](const drift::SkipTo& skip) {
                     group.syncEnabled = false;
                     ++group.corrections;
                     group.correction = SyncCorrection::Skip;
                     clearCorrection();
                     group.correction = SyncCorrection::Skip;
                     // Where the group will be once the seek is done.
                     const auto ahead = groupPosition(timeline, m_offset.toServer(current) + seekLead());
                     seekTo(ahead.value_or(skip.position), ReadyPurpose::Sync);
                     group.cooldownTimer = arm(TimerPurpose::CorrectionCooldown,
                                               current + m_settings.correctionCooldown);
                   },
               },
               decision.action);
  }

  TimerId PlaybackSession::arm(TimerPurpose purpose, LocalInstant due) {
    TimerId id;
    id.serial = ++m_timerSerial;
    id.purpose = purpose;
    if (purpose != TimerPurpose::ReadyDeadline && m_group) id.group = m_group->epoch;
    m_armed.emplace(id.serial, id);
    m_ports.timers.arm(id, due);
    return id;
  }

  void PlaybackSession::cancel(std::optional<TimerId>& timer) {
    if (!timer) return;
    m_armed.erase(timer->serial);
    m_ports.timers.cancel(*timer);
    timer.reset();
  }

  // ---- Snapshot ------------------------------------------------------------

  SessionSnapshot PlaybackSession::buildSnapshot() const {
    SessionSnapshot s;
    s.personalRate = m_personalRate;
    s.effectiveRate = effectiveRate();
    s.error = m_error;
    if (m_media) {
      s.media = m_media->epoch;
      s.item = m_media->request.item;
      s.mediaSource = m_media->request.mediaSource;
      s.title = m_media->request.title;
      for (const auto& track : m_media->tracks) s.externalTracks.push_back(track.state);
      s.position = m_media->position.value_or(m_media->request.start);
      s.duration = m_media->duration;
      if (!m_media->started)
        s.phase = PlaybackPhase::Loading;
      else if (m_media->buffering || (m_media->purpose != ReadyPurpose::None && !m_media->paused))
        s.phase = PlaybackPhase::Buffering;
      else
        s.phase = m_media->paused ? PlaybackPhase::Paused : PlaybackPhase::Playing;
    } else {
      s.phase = m_endPhase;
    }
    if (m_group) {
      GroupSnapshot g;
      g.group = m_group->id;
      g.name = m_group->name;
      g.participants = m_group->participants;
      g.state = m_group->state;
      // The speed the player shows: the group's, or the own file's.
      g.baseRate = groupDrives() ? m_group->baseRate : m_personalRate;
      g.connected = m_group->connected;
      g.playlistItem = m_group->playlistItem;
      g.driftMilliseconds = m_group->driftMilliseconds;
      g.correction = m_group->correction;
      g.correctionsSinceSynced = m_group->corrections;
      if (m_roundTrip)
        g.pingMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(*m_roundTrip).count();
      s.group = g;
    }
    return s;
  }

  void PlaybackSession::publish() {
    SessionSnapshot next = buildSnapshot();
    next.revision = m_snapshot.revision;
    if (next == m_snapshot) return;
    next.revision = m_snapshot.revision + 1;
    m_snapshot = std::move(next);
    m_ports.observer.snapshotChanged(m_snapshot);
  }

} // namespace jfd::core
