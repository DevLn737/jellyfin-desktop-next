#include "SessionHarness.h"

#include <doctest/doctest.h>

using namespace jfd::core;
using namespace jfd::testing;
using namespace std::chrono_literals;

TEST_CASE("Local playback loads, starts at the requested position and plays") {
  SessionHarness h;
  h.post(intent::Open{h.media({}, 30'000)});
  CHECK(h.snapshot().phase == PlaybackPhase::Loading);
  const auto load = h.backend.last<command::Load>();
  REQUIRE(load);
  CHECK_FALSE(load->request.startPaused);

  h.post(backend::SourceOpened{load->epoch, 0.0, 600.0});
  const auto start = h.backend.last<command::Start>();
  REQUIRE(start);
  CHECK(start->position->value == doctest::Approx(30.0));
  CHECK(h.snapshot().phase == PlaybackPhase::Loading);

  h.post(backend::PlaybackRestarted{load->epoch, h.sample(30.0, false)});
  CHECK(h.snapshot().phase == PlaybackPhase::Playing);
  CHECK(h.snapshot().position.milliseconds() == 30'000);
  CHECK(h.snapshot().duration->milliseconds() == 600'000);
  CHECK_FALSE(h.timers.isArmed(TimerPurpose::ReadyDeadline));
}

TEST_CASE("Events of an earlier file are dropped on entry") {
  SessionHarness h;
  const auto first = h.open(h.media());
  h.post(intent::Open{h.media({}, 5'000)});
  const auto second = h.currentEpoch();
  CHECK(first != second);
  CHECK(h.backend.last<command::Stop>()->epoch == first);

  const auto before = h.session->droppedInputs();
  h.post(backend::PlaybackRestarted{first, h.sample(100.0, false)});
  h.post(backend::Ended{first, backend::EndReason::EndOfFile, {}});
  CHECK(h.session->droppedInputs() == before + 2);
  CHECK(h.snapshot().phase == PlaybackPhase::Loading);
  CHECK(h.snapshot().media == second);
}

TEST_CASE("A preserved stream without a start time fails visibly") {
  SessionHarness h;
  auto request = h.media();
  request.timeline.kind = StreamTimeline::Kind::PreservedMusic;
  h.post(intent::Open{request});
  CHECK(h.backend.last<command::Load>()->request.rebaseStartTime);
  h.post(backend::SourceOpened{h.currentEpoch(), std::nullopt, 100.0});
  CHECK(h.snapshot().phase == PlaybackPhase::Failed);
  CHECK_FALSE(h.snapshot().error.empty());
  CHECK(h.backend.last<command::Stop>());
}

TEST_CASE("Personal rate applies outside a group and returns after leaving") {
  SessionHarness h;
  const auto epoch = h.open(h.media());
  h.post(intent::SetRate{*PlaybackRate::base(1.5)});
  CHECK(h.backend.last<command::SetRate>()->rate.perMille() == 1500);

  h.join("p1", *PlaybackRate::base(1.25));
  CHECK(h.backend.last<command::SetRate>()->rate.perMille() == 1250);
  CHECK(h.snapshot().personalRate.perMille() == 1500);

  h.post(group::Left{h.group});
  CHECK(h.backend.last<command::SetRate>()->rate.perMille() == 1500);
  CHECK(h.backend.last<command::SetRate>()->epoch == epoch);
  CHECK_FALSE(h.snapshot().group);
}

TEST_CASE("Joining asks the app for the group's item and reports Ready only after a real restart") {
  SessionHarness h;
  h.join("p1");
  const auto requested = h.observer.all<notice::GroupItemRequested>();
  REQUIRE(requested.size() == 1);
  CHECK(requested[0].playlistItem == PlaylistItemId("p1"));

  h.post(intent::Open{h.media("p1", 12'000)});
  CHECK(h.backend.last<command::Load>()->request.startPaused);
  h.post(backend::SourceOpened{h.currentEpoch(), 0.0, 600.0});
  CHECK(h.outbox.all<outgoing::Ready>().empty());

  // Paused restart far from the start target: not ready yet.
  h.post(backend::PlaybackRestarted{h.currentEpoch(), h.sample(2.0, true)});
  CHECK(h.outbox.all<outgoing::Ready>().empty());

  h.post(backend::PlaybackRestarted{h.currentEpoch(), h.sample(12.04, true)});
  const auto ready = h.outbox.all<outgoing::Ready>();
  REQUIRE(ready.size() == 1);
  CHECK(ready[0].position.milliseconds() == 12'040);
  CHECK_FALSE(ready[0].isPlaying);
  CHECK(ready[0].playlistItem == PlaylistItemId("p1"));
  CHECK(ready[0].epoch == h.group);
}

TEST_CASE("Group seek reports Ready with the new position, never an earlier restart") {
  SessionHarness h;
  h.join("p1");
  const auto epoch = h.open(h.media("p1"), true);
  h.outbox.sent.clear();

  h.post(h.command(GroupCommandType::Seek, 30'000));
  const auto seek = h.backend.last<command::Seek>();
  REQUIRE(seek);
  CHECK(seek->target.value == doctest::Approx(30.0));

  // A restart from before the seek began must not count.
  h.post(backend::PlaybackRestarted{epoch, h.sample(0.0, true)});
  CHECK(h.outbox.all<outgoing::Ready>().empty());

  h.post(backend::SeekStarted{epoch});
  h.post(backend::PlaybackRestarted{epoch, h.sample(30.01, true)});
  const auto ready = h.outbox.all<outgoing::Ready>();
  REQUIRE(ready.size() == 1);
  CHECK(ready[0].position.milliseconds() == 30'010);
}

TEST_CASE("A future unpause waits for its instant; a late one joins the group position") {
  SessionHarness h;
  h.join("p1");
  const auto epoch = h.open(h.media("p1"), true);

  h.post(h.command(GroupCommandType::Unpause, 0, 500ms));
  CHECK_FALSE(h.backend.last<command::SetPaused>());
  h.advance(499ms);
  CHECK_FALSE(h.backend.last<command::SetPaused>());
  h.advance(1ms);
  REQUIRE(h.backend.last<command::SetPaused>());
  CHECK_FALSE(h.backend.last<command::SetPaused>()->paused);

  // A second client receiving the same command 2 s late at 1.5x seeks to 3 s.
  SessionHarness late;
  late.join("p1", *PlaybackRate::base(1.5));
  late.open(late.media("p1"), true);
  auto command = late.command(GroupCommandType::Unpause, 0, -2000ms, *PlaybackRate::base(1.5));
  late.post(command);
  REQUIRE(late.backend.last<command::SetPaused>());
  const auto seek = late.backend.last<command::Seek>();
  REQUIRE(seek);
  CHECK(seek->target.value == doctest::Approx(3.0));
  CHECK_FALSE(late.backend.last<command::SetPaused>()->paused);
  (void)epoch;
}

TEST_CASE("Drift is corrected by speed after the cooldown, then the base rate returns") {
  SessionHarness h;
  h.join("p1");
  const auto epoch = h.open(h.media("p1"), true);
  h.post(h.command(GroupCommandType::Unpause, 0));
  h.post(backend::Progress{epoch, h.sample(0.0, false)});
  CHECK(h.snapshot().group->driftMilliseconds == 0);

  h.advance(2000ms);
  // 300 ms behind the group.
  h.post(backend::Progress{epoch, h.sample(1.7, false)});
  CHECK(h.snapshot().group->driftMilliseconds == 300);
  CHECK(h.snapshot().group->correction == SyncCorrection::Speed);
  CHECK(h.backend.last<command::SetRate>()->rate.perMille() == 1300);
  CHECK(h.snapshot().effectiveRate.perMille() == 1300);

  h.advance(1000ms);
  CHECK(h.backend.last<command::SetRate>()->rate.perMille() == 1000);
  CHECK(h.snapshot().group->correction == SyncCorrection::None);
}

TEST_CASE("Sync correction settings change while playing: speed off, a lower seek threshold") {
  SessionHarness h;
  h.join("p1");
  const auto epoch = h.open(h.media("p1"), true);
  h.post(h.command(GroupCommandType::Unpause, 0));
  h.post(backend::Progress{epoch, h.sample(0.0, false)});

  // Speed off: 300 ms behind stays under the default 400 ms seek threshold.
  ReconcilerSettings settings;
  settings.useSpeed = false;
  h.post(intent::SetSyncCorrection{settings});
  h.advance(2000ms);
  h.post(backend::Progress{epoch, h.sample(1.7, false)});
  CHECK(h.snapshot().group->driftMilliseconds == 300);
  CHECK(h.snapshot().group->correction == SyncCorrection::None);
  CHECK(h.snapshot().effectiveRate.perMille() == 1000);
  CHECK_FALSE(h.backend.last<command::Seek>());

  // With the threshold at 200 ms the same drift is a seek to the group.
  settings.skipMinimum = MediaDelta::fromMilliseconds(200);
  h.post(intent::SetSyncCorrection{settings});
  h.advance(2000ms);
  h.post(backend::Progress{epoch, h.sample(3.7, false)});
  CHECK(h.snapshot().group->correction == SyncCorrection::Skip);
  REQUIRE(h.backend.last<command::Seek>());
  CHECK(h.backend.last<command::Seek>()->target.value == doctest::Approx(4.0).epsilon(0.01));
}

TEST_CASE("Leaving cancels a pending correction before restoring the personal rate") {
  SessionHarness h;
  const auto epoch = h.open(h.media("p1"));
  h.post(intent::SetRate{*PlaybackRate::base(2.0)});
  h.join("p1");
  h.post(h.command(GroupCommandType::Unpause, 0));
  h.advance(2000ms);
  h.post(backend::Progress{epoch, h.sample(1.7, false)});
  REQUIRE(h.timers.isArmed(TimerPurpose::CorrectionEnd));

  h.post(group::Left{h.group});
  CHECK_FALSE(h.timers.isArmed(TimerPurpose::CorrectionEnd));
  CHECK(h.backend.last<command::SetRate>()->rate.perMille() == 2000);
  h.advance(5000ms);
  CHECK(h.backend.last<command::SetRate>()->rate.perMille() == 2000);
}

TEST_CASE("Intents inside a group go to the group") {
  SessionHarness h;
  h.join("p1");
  h.open(h.media("p1"), true);
  h.outbox.sent.clear();
  h.post(intent::Play{});
  h.post(intent::Pause{});
  h.post(intent::Seek{SessionHarness::ms(5'000)});
  h.post(intent::SetRate{*PlaybackRate::base(1.25)});
  h.post(intent::SetRate{*PlaybackRate::effective(0.2)});
  CHECK(h.outbox.all<outgoing::RequestUnpause>().size() == 1);
  CHECK(h.outbox.all<outgoing::RequestPause>().size() == 1);
  CHECK(h.outbox.all<outgoing::RequestSeek>()[0].position.milliseconds() == 5'000);
  const auto rates = h.outbox.all<outgoing::RequestPlaybackRate>();
  REQUIRE(rates.size() == 1);
  CHECK(rates[0].rate.perMille() == 1250);

  h.post(group::RateChangeRejected{h.group, 409, 1});
  const auto rejected = h.observer.all<notice::GroupRateRejected>();
  REQUIRE(rejected.size() == 1);
  CHECK(rejected[0].httpStatus == 409);
}

TEST_CASE("Buffering while the group plays is reported, and Ready on recovery") {
  SessionHarness h;
  h.join("p1");
  const auto epoch = h.open(h.media("p1"), true);
  h.post(h.command(GroupCommandType::Unpause, 0));
  h.outbox.sent.clear();
  h.advance(1000ms);
  h.post(backend::Progress{epoch, h.sample(1.0, false, true)});
  REQUIRE(h.outbox.all<outgoing::Buffering>().size() == 1);
  CHECK(h.outbox.all<outgoing::Buffering>()[0].isPlaying);
  h.post(backend::Progress{epoch, h.sample(1.0, false, false)});
  CHECK(h.outbox.all<outgoing::Ready>().size() == 1);
}

TEST_CASE("Old play queue updates and messages of an earlier group are ignored") {
  SessionHarness h;
  h.join("p1", *PlaybackRate::base(1.5));
  group::QueueChanged old;
  old.epoch = h.group;
  old.reason = QueueChangeReason::SetCurrentItem;
  old.lastUpdate = h.serverNow() - 1s;
  old.currentPlaylistItem = PlaylistItemId("p0");
  old.baseRate = PlaybackRate::normal();
  h.post(old);
  CHECK(h.snapshot().group->baseRate.perMille() == 1500);
  CHECK(h.snapshot().group->playlistItem == PlaylistItemId("p1"));

  const auto previous = h.group;
  h.join("p1");
  const auto dropped = h.session->droppedInputs();
  h.post(group::Left{previous});
  CHECK(h.session->droppedInputs() == dropped + 1);
  CHECK(h.snapshot().group);
}

TEST_CASE("A seek before the start of an offset transcode asks for a new stream") {
  SessionHarness h;
  auto request = h.media({}, 600'000);
  request.timeline.offset = SessionHarness::ms(600'000);
  h.open(request);
  h.post(intent::Seek{SessionHarness::ms(10'000)});
  const auto reopen = h.observer.all<notice::StreamReopenNeeded>();
  REQUIRE(reopen.size() == 1);
  CHECK(reopen[0].position.milliseconds() == 10'000);
}

TEST_CASE("Snapshots are published only when they change") {
  SessionHarness h;
  h.open(h.media());
  const auto count = h.observer.snapshots.size();
  h.post(group::ClockSynced{{}, WallTicks(0)});
  CHECK(h.observer.snapshots.size() == count);
  CHECK(h.observer.snapshots.back().revision == h.snapshot().revision);
}

TEST_CASE("External tracks added before the file opens are issued once it opens") {
  SessionHarness h;
  h.post(intent::Open{h.media()});
  const auto epoch = h.currentEpoch();
  const ExternalTrack audio{TrackKind::Audio, "http://server/audio.mka", "Commentary", "en"};
  h.post(intent::AddExternalTrack{epoch, audio});
  CHECK(h.backend.all<command::AddTrack>().empty());
  REQUIRE(h.snapshot().externalTracks.size() == 1);
  CHECK(h.snapshot().externalTracks[0].status == ExternalTrackState::Status::Adding);

  h.post(backend::SourceOpened{epoch, 0.0, 600.0});
  const auto added = h.backend.all<command::AddTrack>();
  REQUIRE(added.size() == 1);
  CHECK(added[0].track == audio);

  h.post(backend::TrackAdded{epoch, added[0].request, 3});
  CHECK(h.snapshot().externalTracks[0].status == ExternalTrackState::Status::Added);
  CHECK(h.snapshot().externalTracks[0].trackId == 3);
}

TEST_CASE("A failed external track is shown and does not touch readiness") {
  SessionHarness h;
  const auto epoch = h.open(h.media());
  h.post(intent::Seek{SessionHarness::ms(20'000)});
  const auto seek = h.backend.last<command::Seek>();
  REQUIRE(seek);
  h.post(intent::AddExternalTrack{epoch, {TrackKind::Subtitle, "http://server/missing.srt", {}, {}}});
  const auto add = h.backend.last<command::AddTrack>();
  REQUIRE(add);

  h.post(backend::RequestFailed{epoch, add->request, "not found"});
  CHECK(h.snapshot().externalTracks[0].status == ExternalTrackState::Status::Failed);
  CHECK(h.snapshot().externalTracks[0].error == "not found");
  // The seek is still pending and completes as usual.
  CHECK(h.timers.isArmed(TimerPurpose::ReadyDeadline));
  h.post(backend::SeekStarted{epoch});
  h.post(backend::PlaybackRestarted{epoch, h.sample(20.0, false)});
  CHECK_FALSE(h.timers.isArmed(TimerPurpose::ReadyDeadline));
}

TEST_CASE("Track intents for a replaced file are dropped") {
  SessionHarness h;
  const auto first = h.open(h.media());
  h.open(h.media());
  const auto dropped = h.session->droppedInputs();
  h.post(intent::SelectTrack{first, TrackKind::Audio, 2});
  h.post(intent::AddExternalTrack{first, {TrackKind::Audio, "http://server/a.mka", {}, {}}});
  CHECK(h.session->droppedInputs() == dropped + 2);
  CHECK(h.backend.all<command::SelectTrack>().empty());
  CHECK(h.backend.all<command::AddTrack>().empty());
  CHECK(h.snapshot().externalTracks.empty());
}

TEST_CASE("Selecting a track in a group stays local") {
  SessionHarness h;
  h.join("p1");
  const auto epoch = h.open(h.media("p1"), true);
  h.post(h.command(GroupCommandType::Unpause, 0));
  h.outbox.sent.clear();
  h.post(intent::SelectTrack{epoch, TrackKind::Subtitle, std::nullopt});
  const auto select = h.backend.last<command::SelectTrack>();
  REQUIRE(select);
  CHECK(select->kind == TrackKind::Subtitle);
  CHECK_FALSE(select->trackId);
  CHECK(h.outbox.sent.empty());
}

TEST_CASE("Next and Previous go to the group, or to the app's queue outside one") {
  SessionHarness h;
  h.open(h.media());
  h.post(intent::Next{});
  h.post(intent::Previous{});
  const auto steps = h.observer.all<notice::QueueStepRequested>();
  REQUIRE(steps.size() == 2);
  CHECK(steps[0].forward);
  CHECK_FALSE(steps[1].forward);

  h.join("p7");
  h.post(intent::Next{});
  h.post(intent::Previous{});
  const auto next = h.outbox.all<outgoing::RequestNextItem>();
  REQUIRE(next.size() == 1);
  CHECK(next[0].playlistItem == PlaylistItemId("p7"));
  CHECK(next[0].epoch == h.group);
  CHECK(h.outbox.all<outgoing::RequestPreviousItem>().size() == 1);
  CHECK(h.observer.all<notice::QueueStepRequested>().size() == 2);
}

TEST_CASE("The snapshot carries the title, the group name and its participants") {
  SessionHarness h;
  auto request = h.media("p1");
  request.title = "Episode 3";
  h.group = h.groupEpochs.advance();
  h.post(group::Joined{h.group, GroupId("g"), "Movie night", {"anna", "boris"}});
  CHECK(h.snapshot().group->name == "Movie night");
  CHECK(h.snapshot().group->participants == std::vector<std::string>{"anna", "boris"});
  h.post(group::ParticipantsChanged{h.group, {"anna", "boris", "anna"}});
  CHECK(h.snapshot().group->participants.size() == 3);

  h.post(intent::Open{request});
  CHECK(h.snapshot().title == "Episode 3");
}

TEST_CASE("Buffering that ends during the group's pause seek is followed by Ready") {
  SessionHarness h;
  h.join("p1");
  const auto epoch = h.open(h.media("p1"), true);
  h.post(h.command(GroupCommandType::Unpause, 0));
  h.advance(1000ms);
  h.post(backend::Progress{epoch, h.sample(1.0, false, true)});
  REQUIRE(h.outbox.all<outgoing::Buffering>().size() == 1);
  h.outbox.sent.clear();

  // The group pauses everyone; our buffering ends while that seek runs.
  h.post(h.command(GroupCommandType::Pause, 1'000));
  h.post(backend::Progress{epoch, h.sample(1.0, true, false)});
  CHECK(h.outbox.all<outgoing::Ready>().empty());
  h.post(backend::SeekStarted{epoch});
  h.post(backend::PlaybackRestarted{epoch, h.sample(1.0, true)});
  h.post(backend::Progress{epoch, h.sample(1.0, true)});
  const auto ready = h.outbox.all<outgoing::Ready>();
  REQUIRE(ready.size() == 1);
  CHECK_FALSE(ready[0].isPlaying);
  h.post(backend::Progress{epoch, h.sample(1.0, true)});
  CHECK(h.outbox.all<outgoing::Ready>().size() == 1);
}
