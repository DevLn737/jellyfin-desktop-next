#include "../support/DoctestQt.h"

#include <jfd/playback/TrackController.h>

#include <algorithm>
#include <vector>

using namespace jfd;
using namespace jfd::playback;
using core::TrackKind;
using Status = core::ExternalTrackState::Status;

namespace {

  api::MediaStream stream(int index, const char* type, const char* delivery = "",
                          bool external = false) {
    api::MediaStream s;
    s.index = index;
    s.type = QString::fromLatin1(type);
    s.deliveryMethod = QString::fromLatin1(delivery);
    s.isExternal = external;
    return s;
  }

  const char* const SubtitleUrl = "https://srv/Subtitles/0/Stream.srt";
  const char* const AudioUrl = "https://srv/Audio/x/stream.mka";

  struct Fixture {
    api::MediaSource source;
    api::PlaybackStream played;
    std::vector<core::SessionInput> posted;
    std::vector<command::Reopen> reopens;
    std::vector<std::pair<std::optional<int>, std::optional<int>>> selections;
    TrackController controller{{
        [this](core::SessionInput input) { posted.push_back(std::move(input)); },
        [this](const command::Reopen& reopen) { reopens.push_back(reopen); },
        [this](std::optional<int> audio, std::optional<int> subtitle) {
          selections.emplace_back(audio, subtitle);
        },
    }};
    core::EpochCounter<core::MediaEpochTag> files;
    core::MediaEpoch epoch = files.advance();
    core::SessionSnapshot snapshot;

    explicit Fixture(api::PlayMethod method = api::PlayMethod::DirectPlay) {
      source.mediaStreams = {stream(0, "Subtitle", "External", true),
                             stream(1, "Audio", "External", true), stream(2, "Video"),
                             stream(3, "Audio"), stream(4, "Audio"),
                             stream(5, "Subtitle", "Embed")};
      played.method = method;
      api::ExternalTrack subtitle;
      subtitle.streamIndex = 0;
      subtitle.url = QUrl(QString::fromLatin1(SubtitleUrl));
      api::ExternalTrack audio;
      audio.streamIndex = 1;
      audio.url = QUrl(QString::fromLatin1(AudioUrl));
      played.externalSubtitles = {subtitle};
      played.externalAudio = {audio};
      snapshot.media = epoch;
    }

    template <class T> const T& last() const {
      REQUIRE_FALSE(posted.empty());
      REQUIRE(std::holds_alternative<T>(posted.back()));
      return std::get<T>(posted.back());
    }

    void answer(const char* url, Status status, std::optional<int> trackId = std::nullopt) {
      for (auto& state : snapshot.externalTracks)
        if (state.track.url == url) {
          state.status = status;
          state.trackId = trackId;
        }
      controller.snapshotChanged(snapshot);
    }

    // What the session lists after the adds posted so far.
    void listAdds() {
      for (const auto& input : posted)
        if (const auto* add = std::get_if<core::intent::AddExternalTrack>(&input)) {
          const bool listed = std::any_of(
              snapshot.externalTracks.begin(), snapshot.externalTracks.end(),
              [&](const core::ExternalTrackState& s) { return s.track.url == add->track.url; });
          if (listed) continue;
          core::ExternalTrackState state;
          state.track = add->track;
          snapshot.externalTracks.push_back(state);
        }
      controller.snapshotChanged(snapshot);
    }
  };

} // namespace

TEST_CASE("Initial tracks become session intents for the file's epoch") {
  Fixture f;
  f.controller.begin(f.epoch, f.source, f.played, 4, 5);
  REQUIRE(f.posted.size() == 2);
  const auto& audio = std::get<core::intent::SelectTrack>(f.posted[0]);
  CHECK(audio.epoch == f.epoch);
  CHECK(audio.kind == TrackKind::Audio);
  CHECK(audio.trackId == 2);
  CHECK(std::get<core::intent::SelectTrack>(f.posted[1]).trackId == 1);
  REQUIRE(f.selections.size() == 1);
  CHECK(f.selections.back() == std::pair<std::optional<int>, std::optional<int>>{4, 5});
}

TEST_CASE("An external track is selected by the id the session reports") {
  Fixture f;
  f.controller.begin(f.epoch, f.source, f.played, 3, std::nullopt);
  f.posted.clear();
  f.controller.select(TrackKind::Subtitle, 0);
  CHECK(f.last<core::intent::AddExternalTrack>().track.url == SubtitleUrl);
  CHECK(f.selections.back().second == 0);
  f.listAdds();
  CHECK(f.posted.size() == 1);

  f.answer(SubtitleUrl, Status::Added, 7);
  const auto& select = f.last<core::intent::SelectTrack>();
  CHECK(select.kind == TrackKind::Subtitle);
  CHECK(select.trackId == 7);
  // Later snapshots repeat the answer; it is applied once.
  const auto count = f.posted.size();
  f.controller.snapshotChanged(f.snapshot);
  CHECK(f.posted.size() == count);
}

TEST_CASE("A failed external audio file falls back and is reported") {
  Fixture f;
  f.controller.begin(f.epoch, f.source, f.played, 1, std::nullopt);
  REQUIRE(std::holds_alternative<core::intent::AddExternalTrack>(f.posted.front()));
  CHECK(std::get<core::intent::AddExternalTrack>(f.posted.front()).track.url == AudioUrl);
  f.listAdds();
  f.answer(AudioUrl, Status::Failed);
  CHECK(f.last<core::intent::SelectTrack>().trackId == 1);
  CHECK(f.selections.back().first == 3);
  CHECK(f.controller.table().failed(1));
}

TEST_CASE("Snapshots of another file are ignored") {
  Fixture f;
  f.controller.begin(f.epoch, f.source, f.played, 1, std::nullopt);
  f.listAdds();
  const auto count = f.posted.size();
  f.snapshot.media = f.files.advance();
  f.answer(AudioUrl, Status::Added, 4);
  CHECK(f.posted.size() == count);
}

TEST_CASE("A track the transcode does not carry asks for a reopen") {
  Fixture f(api::PlayMethod::Transcode);
  f.controller.begin(f.epoch, f.source, f.played, 4, std::nullopt);
  f.posted.clear();
  f.controller.select(TrackKind::Audio, 3);
  CHECK(f.posted.empty());
  REQUIRE(f.reopens.size() == 1);
  CHECK(f.reopens.front().audioStreamIndex == 3);
  CHECK(f.reopens.front().subtitleStreamIndex == -1);
}
