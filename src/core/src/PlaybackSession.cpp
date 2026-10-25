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
      if (accept(next)) {
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
            [&](const group::Joined& joined) {
              return joined.epoch.isValid() && joined.epoch > m_latestGroupEpoch;
            },
            [&](const group::ClockSynced&) { return true; },
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

  // ---- Intents -------------------------------------------------------------

  void PlaybackSession::on(const intent::Open& open) {
    if (m_media) m_ports.backend.stop(m_media->epoch);
    Media media;
    media.epoch = m_mediaEpochs.advance();
    media.request = open.media;
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

  void PlaybackSession::on(const intent::Play&) {
    if (m_group) {
      m_ports.syncPlay.send(outgoing::RequestUnpause{m_group->epoch});
      return;
    }
    setPaused(false);
  }

  void PlaybackSession::on(const intent::Pause&) {
    if (m_group) {
      m_ports.syncPlay.send(outgoing::RequestPause{m_group->epoch});
      return;
    }
    setPaused(true);
  }

  void PlaybackSession::on(const intent::Seek& seek) {
    if (m_group) {
      m_ports.syncPlay.send(outgoing::RequestSeek{m_group->epoch, seek.position});
      return;
    }
    seekTo(seek.position, ReadyPurpose::Local);
  }

  void PlaybackSession::on(const intent::SetRate& rate) {
    if (m_group) {
      // The group rate is a base rate; the server validates the same range.
      if (const auto base = PlaybackRate::basePerMille(rate.rate.perMille()))
        m_ports.syncPlay.send(outgoing::RequestPlaybackRate{m_group->epoch, *base});
      return;
    }
    if (!PlaybackRate::basePerMille(rate.rate.perMille())) return;
    m_personalRate = rate.rate;
    applyEffectiveRate();
  }

  void PlaybackSession::on(const intent::Stop&) {
    if (m_group) {
      m_ports.syncPlay.send(outgoing::RequestStop{m_group->epoch});
      return;
    }
    stopMedia(PlaybackPhase::Idle);
  }

  void PlaybackSession::on(const intent::Next&) { stepQueue(true); }

  void PlaybackSession::on(const intent::Previous&) { stepQueue(false); }

  void PlaybackSession::on(const intent::SetSyncCorrection& set) { m_settings.reconciler = set.reconciler; }

  void PlaybackSession::stepQueue(bool forward) {
    if (!m_group) {
      m_ports.observer.notify(notice::QueueStepRequested{forward});
      return;
    }
    if (m_group->playlistItem.isEmpty()) return;
    if (forward)
      m_ports.syncPlay.send(outgoing::RequestNextItem{m_group->epoch, m_group->playlistItem});
    else
      m_ports.syncPlay.send(outgoing::RequestPreviousItem{m_group->epoch, m_group->playlistItem});
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
    if (!completed) return;
    const ReadyPurpose purpose = media.purpose;
    media.purpose = ReadyPurpose::None;
    media.target.reset();
    cancel(media.readyDeadline);
    media.started = true;
    readyConfirmed(purpose);
  }

  void PlaybackSession::on(const backend::Progress& progress) {
    const bool wasBuffering = m_media->buffering;
    applySample(progress.sample);
    if (!m_group || !mediaFollowsGroup() || !m_media->started) return;
    if (m_media->purpose == ReadyPurpose::None) {
      if (m_media->buffering != wasBuffering) {
        // Leaving any correction while the backend waits for data, as Web does.
        if (m_media->buffering) clearCorrection();
        reportReady(!m_media->buffering);
      } else if (!m_media->buffering && m_group->reportedBuffering) {
        // Buffering ended during a seek (the group's Pause answers our
        // Buffering with one): the group waits for our Ready until we send it.
        reportReady(true);
      }
    }
    synchronize();
  }

  void PlaybackSession::on(const backend::Ended& ended) {
    switch (ended.reason) {
    case backend::EndReason::EndOfFile:
      stopMedia(PlaybackPhase::Ended);
      break;
    case backend::EndReason::Stopped:
      stopMedia(PlaybackPhase::Idle);
      break;
    case backend::EndReason::Error:
      stopMedia(PlaybackPhase::Failed, ended.error.empty() ? "Playback failed." : ended.error);
      break;
    }
  }

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
    applyEffectiveRate();
    if (!changesCurrentItem(queue.reason)) return;
    const bool itemChanged = group.playlistItem != queue.currentPlaylistItem;
    group.playlistItem = queue.currentPlaylistItem;
    if (group.playlistItem.isEmpty()) return;

    if (m_media && m_media->request.playlistItem == group.playlistItem) {
      // Rejoining (or a queue edit) with this occurrence already open: report
      // where we are instead of reloading, now or once a pending seek is done.
      if (!m_media->started)
        m_media->purpose = ReadyPurpose::GroupOpen;
      else if (m_media->purpose == ReadyPurpose::None)
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

  void PlaybackSession::on(const group::ClockSynced& synced) { m_offset = synced.offset; }

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
      if (command.type == GroupCommandType::Stop && m_media) stopMedia(PlaybackPhase::Idle);
      return;
    }

    const LocalInstant due = m_offset.toLocal(command.when);
    if (due > now()) {
      if (command.type == GroupCommandType::Unpause && m_media->position &&
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
    case TimerPurpose::ReadyDeadline:
      if (!m_media || m_media->readyDeadline != fired.id) break;
      m_media->readyDeadline.reset();
      m_ports.observer.notify(notice::ReadyTimedOut{m_media->target.value_or(MediaTicks{})});
      if (m_media->purpose == ReadyPurpose::GroupSeek && m_media->target) {
        // As Jellyfin Web does after its ready timeout: seek again.
        seekTo(*m_media->target, ReadyPurpose::GroupSeek);
      }
      break;
    }
  }

  // ---- Helpers -------------------------------------------------------------

  bool PlaybackSession::mediaFollowsGroupItem(const PlaylistItemId& item) const {
    return m_group && !item.isEmpty() && item == m_group->playlistItem;
  }

  bool PlaybackSession::mediaFollowsGroup() const {
    return m_media && mediaFollowsGroupItem(m_media->request.playlistItem);
  }

  void PlaybackSession::stopMedia(PlaybackPhase endPhase, std::string error) {
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
    cancel(media.readyDeadline);
    if (purpose == ReadyPurpose::GroupSeek || purpose == ReadyPurpose::Local)
      media.readyDeadline = arm(TimerPurpose::ReadyDeadline, now() + m_settings.readyDeadline);
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
    if (!m_group) return m_personalRate;
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
    case ReadyPurpose::None:
    case ReadyPurpose::Local:
    case ReadyPurpose::Sync:
      break;
    }
  }

  void PlaybackSession::reportReady(bool ready) {
    if (!m_group || !m_media || !m_media->position) return;
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
      m_ports.syncPlay.send(outgoing::Ready{m_group->epoch, when, position, playing,
                                            m_media->request.playlistItem});
    } else {
      m_group->reportedBuffering = true;
      m_ports.syncPlay.send(outgoing::Buffering{m_group->epoch, when, position, !m_media->paused,
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
        const auto target = groupPosition(
            GroupTimeline{command.position, command.when, command.baseRate}, serverNow());
        if (target && (!m_media->position ||
                       (*target - *m_media->position).abs() >= m_settings.reconciler.speedMinimum))
          seekTo(*target, ReadyPurpose::Sync);
      }
      setPaused(false);
      m_group->cooldownTimer =
          arm(TimerPurpose::CorrectionCooldown, now() + m_settings.correctionCooldown);
      break;
    }
    case GroupCommandType::Pause:
      setPaused(true);
      seekTo(command.position, ReadyPurpose::Sync);
      break;
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
    // Already scheduled: nothing to do.
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
        media.purpose != ReadyPurpose::None)
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
                     seekTo(skip.position, ReadyPurpose::Sync);
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
      g.baseRate = m_group->baseRate;
      g.connected = m_group->connected;
      g.playlistItem = m_group->playlistItem;
      g.driftMilliseconds = m_group->driftMilliseconds;
      g.correction = m_group->correction;
      g.correctionsSinceSynced = m_group->corrections;
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
