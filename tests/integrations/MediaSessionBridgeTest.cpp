#include "../support/DoctestQt.h"

#include <jfd/integrations/MediaSessionBridge.h>
#include <jfd/platform/SleepInhibitor.h>

#include <vector>

using namespace jfd::integrations;
using jfd::core::ItemId;
using jfd::core::MediaTicks;
using jfd::core::PlaybackPhase;
using jfd::core::SessionInput;
using jfd::core::SessionSnapshot;
using jfd::platform::MediaCommand;
using jfd::platform::MediaControlsState;
namespace intent = jfd::core::intent;

namespace {

  SessionSnapshot playing(PlaybackPhase phase = PlaybackPhase::Playing) {
    SessionSnapshot snapshot;
    snapshot.phase = phase;
    snapshot.item = ItemId("episode-1");
    snapshot.title = "The Pilot";
    snapshot.position = *MediaTicks::fromMilliseconds(65'000);
    snapshot.duration = MediaTicks::fromMilliseconds(1'800'000);
    return snapshot;
  }

  NowPlayingDetails episode() {
    NowPlayingDetails details;
    details.item = ItemId("episode-1");
    details.kind = PresenceInfo::Kind::Episode;
    details.seriesName = QStringLiteral("Show");
    details.season = 1;
    details.episode = 2;
    details.artUrl = QUrl(QStringLiteral("https://media.example.org/Items/s/Images/Primary"));
    details.publicPoster = details.artUrl;
    details.hasNext = true;
    return details;
  }

  class FakeControls final : public jfd::platform::MediaControls {
  public:
    bool isAvailable() const override { return true; }
    void attachWindow(QWindow*) override {}
    void setState(const MediaControlsState& state) override { states.push_back(state); }
    std::vector<MediaControlsState> states;
  };

  class FakeInhibitor final : public jfd::platform::SleepInhibitor {
  public:
    void setInhibited(bool inhibited, const QString& reason) override {
      calls.push_back(inhibited);
      lastReason = reason;
    }
    std::vector<bool> calls;
    QString lastReason;
  };

} // namespace

TEST_CASE("Media controls show the item, its position and what the queue allows") {
  const auto details = episode();
  const auto state = mediaControlsState(playing(), &details);
  CHECK(state.status == MediaControlsState::Status::Playing);
  CHECK(state.title == QStringLiteral("The Pilot"));
  CHECK(state.subtitle == QStringLiteral("Show · S1 E2"));
  CHECK(state.positionMs == 65'000);
  CHECK(state.durationMs == 1'800'000);
  CHECK(state.canSeek);
  CHECK(state.canGoNext);
  CHECK_FALSE(state.canGoPrevious);
  CHECK(state.artUrl == details.artUrl);

  CHECK(mediaControlsState(playing(PlaybackPhase::Paused), &details).status ==
        MediaControlsState::Status::Paused);
  CHECK(mediaControlsState(playing(PlaybackPhase::Buffering), &details).status ==
        MediaControlsState::Status::Playing);
  CHECK(mediaControlsState(playing(PlaybackPhase::Ended), &details) == MediaControlsState{});
  CHECK(mediaControlsState(SessionSnapshot{}, nullptr) == MediaControlsState{});
}

TEST_CASE("A track shows its artist under the title") {
  auto details = episode();
  details.kind = PresenceInfo::Kind::Music;
  details.artist = QStringLiteral("Artist");
  CHECK(mediaControlsState(playing(), &details).subtitle == QStringLiteral("Artist"));
}

TEST_CASE("A group can always step its queue; movies show their year") {
  auto snapshot = playing();
  snapshot.group.emplace();
  snapshot.group->participants = {"alice", "bob", "me"};
  const auto state = mediaControlsState(snapshot, nullptr);
  CHECK(state.canGoNext);
  CHECK(state.canGoPrevious);
  CHECK(state.subtitle.isEmpty());

  NowPlayingDetails movie;
  movie.item = snapshot.item;
  movie.year = 2016;
  CHECK(mediaControlsState(snapshot, &movie).subtitle == QStringLiteral("2016"));

  const auto info = presenceInfo(snapshot, &movie);
  REQUIRE(info);
  CHECK(info->groupSize == 3);
  CHECK(info->year == 2016);
  CHECK(info->playing);
}

TEST_CASE("Presence follows the phase and is removed when nothing plays") {
  const auto details = episode();
  auto info = presenceInfo(playing(PlaybackPhase::Loading), &details);
  REQUIRE(info);
  CHECK_FALSE(info->playing);
  CHECK(info->buffering);
  CHECK(info->seriesName == QStringLiteral("Show"));
  CHECK(info->episode == 2);
  CHECK(info->poster == details.publicPoster);
  CHECK(info->groupSize == 0);
  CHECK_FALSE(presenceInfo(playing(PlaybackPhase::Idle), &details));
  CHECK_FALSE(presenceInfo(playing(PlaybackPhase::Failed), &details));
}

TEST_CASE("Commands become session intents") {
  CHECK(std::holds_alternative<intent::Pause>(*commandInput(MediaCommand::PlayPause, PlaybackPhase::Playing)));
  CHECK(std::holds_alternative<intent::Play>(*commandInput(MediaCommand::PlayPause, PlaybackPhase::Paused)));
  CHECK_FALSE(commandInput(MediaCommand::PlayPause, PlaybackPhase::Idle));
  CHECK(std::holds_alternative<intent::Next>(*commandInput(MediaCommand::Next, PlaybackPhase::Playing)));
  CHECK(std::holds_alternative<intent::Previous>(*commandInput(MediaCommand::Previous, PlaybackPhase::Paused)));
  CHECK(std::holds_alternative<intent::Stop>(*commandInput(MediaCommand::Stop, PlaybackPhase::Playing)));
}

TEST_CASE("The bridge feeds controls and sleep inhibition and posts their commands") {
  std::vector<SessionInput> posted;
  MediaSessionBridge bridge([&](SessionInput input) { posted.push_back(std::move(input)); });
  FakeControls controls;
  FakeInhibitor inhibitor;
  bridge.addControls(&controls);
  bridge.setSleepInhibitor(&inhibitor);
  REQUIRE(controls.states.size() == 1);
  CHECK(controls.states.back().status == MediaControlsState::Status::Stopped);
  CHECK(inhibitor.calls == std::vector<bool>{false});

  // Details for another item are not used.
  auto other = episode();
  other.item = ItemId("another");
  bridge.setDetails(other);
  bridge.update(playing());
  CHECK(controls.states.back().subtitle.isEmpty());
  bridge.setDetails(episode());
  CHECK(controls.states.back().subtitle == QStringLiteral("Show · S1 E2"));
  CHECK(inhibitor.calls == std::vector<bool>{false, true});
  CHECK(inhibitor.lastReason == QStringLiteral("Playing video"));

  // The same snapshot again changes nothing.
  const auto sent = controls.states.size();
  bridge.update(playing());
  CHECK(controls.states.size() == sent);

  Q_EMIT controls.commandRequested(MediaCommand::PlayPause);
  Q_EMIT controls.seekRequested(90'000);
  REQUIRE(posted.size() == 2);
  CHECK(std::holds_alternative<intent::Pause>(posted[0]));
  REQUIRE(std::holds_alternative<intent::Seek>(posted[1]));
  CHECK(std::get<intent::Seek>(posted[1]).position.milliseconds() == 90'000);

  bridge.update(playing(PlaybackPhase::Paused));
  CHECK(inhibitor.calls == std::vector<bool>{false, true, false});
  CHECK(controls.states.back().status == MediaControlsState::Status::Paused);

  bridge.update(SessionSnapshot{});
  Q_EMIT controls.seekRequested(1'000);
  CHECK(posted.size() == 2);
}
