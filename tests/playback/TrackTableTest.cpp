#include "../support/DoctestQt.h"

#include <jfd/playback/TrackTable.h>

using namespace jfd;
using namespace jfd::playback;
using core::TrackKind;

namespace {

  api::MediaStream stream(int index, const char* type, const char* language = "",
                          const char* delivery = "", bool external = false) {
    api::MediaStream s;
    s.index = index;
    s.type = QString::fromLatin1(type);
    s.language = QString::fromLatin1(language);
    s.title = QStringLiteral("%1 %2").arg(s.type).arg(index);
    s.deliveryMethod = QString::fromLatin1(delivery);
    s.isExternal = external;
    return s;
  }

  api::ExternalTrack external(int index, const char* url) {
    api::ExternalTrack track;
    track.streamIndex = index;
    track.url = QUrl(QString::fromLatin1(url));
    return track;
  }

  // As Jellyfin lists a file with an external .srt and an external audio
  // file: externals first with the lowest indices, then the embedded streams.
  struct Movie {
    api::MediaSource source;
    api::PlaybackStream played;

    explicit Movie(api::PlayMethod method = api::PlayMethod::DirectPlay) {
      source.mediaStreams = {
          stream(0, "Subtitle", "rus", "External", true),
          stream(1, "Audio", "rus", "External", true),
          stream(2, "Video"),
          stream(3, "Audio", "jpn"),
          stream(4, "Audio", "eng"),
          stream(5, "Subtitle", "eng", "Embed"),
          stream(6, "Subtitle", "rus", "Encode"),
      };
      played.method = method;
      played.externalSubtitles = {external(0, "https://srv/Subtitles/0/Stream.srt?ApiKey=t")};
      played.externalAudio = {external(1, "https://srv/Audio/x/stream.mka?ApiKey=t")};
    }
  };

  template <class T> const T& only(const std::vector<Command>& commands) {
    REQUIRE(commands.size() == 1);
    REQUIRE(std::holds_alternative<T>(commands.front()));
    return std::get<T>(commands.front());
  }

} // namespace

TEST_CASE("Embedded tracks map to mpv ids by order among embedded streams") {
  Movie movie;
  TrackTable table(movie.source, movie.played);
  CHECK(table.tracks(TrackKind::Audio).size() == 3);
  CHECK(table.tracks(TrackKind::Subtitle).size() == 3);

  CHECK(only<command::Select>(table.select(TrackKind::Audio, 4)).trackId == 2);
  CHECK(only<command::Select>(table.select(TrackKind::Subtitle, 6)).trackId == 2);
  CHECK(table.selected(TrackKind::Subtitle) == 6);
  CHECK_FALSE(only<command::Select>(table.select(TrackKind::Subtitle, std::nullopt)).trackId);
  CHECK(table.select(TrackKind::Audio, 42).empty());
  CHECK(table.selected(TrackKind::Audio) == 4);
}

TEST_CASE("An external track is added once and then selected by its mpv id") {
  Movie movie;
  TrackTable table(movie.source, movie.played);
  const auto initial = table.selectInitial(1, 0);
  REQUIRE(initial.size() == 2);
  const auto& audio = std::get<command::Add>(initial[0]);
  const auto& subtitle = std::get<command::Add>(initial[1]);
  CHECK(audio.track.kind == TrackKind::Audio);
  CHECK(audio.track.url == "https://srv/Audio/x/stream.mka?ApiKey=t");
  CHECK(subtitle.track.kind == TrackKind::Subtitle);
  CHECK(audio.key != subtitle.key);

  // Selecting it again while the add is in flight sends nothing new.
  CHECK(table.select(TrackKind::Audio, 1).empty());
  CHECK(only<command::Select>(table.trackAdded(audio.key, 3)).trackId == 3);

  table.select(TrackKind::Audio, 3);
  CHECK(only<command::Select>(table.select(TrackKind::Audio, 1)).trackId == 3);
}

TEST_CASE("A late add does not override a newer choice") {
  Movie movie;
  TrackTable table(movie.source, movie.played);
  const auto add = only<command::Add>(table.select(TrackKind::Audio, 1));
  CHECK(only<command::Select>(table.select(TrackKind::Audio, 3)).trackId == 1);

  // mpv selected the late track when it added it; the table puts jpn back.
  CHECK(only<command::Select>(table.trackAdded(add.key, 3)).trackId == 1);
  CHECK(table.selected(TrackKind::Audio) == 3);
  // A reply for an unknown or already answered key is ignored.
  CHECK(table.trackAdded(add.key, 3).empty());
  CHECK(table.trackAdded(999, 1).empty());
}

TEST_CASE("A track that fails to load leaves the menu, and audio falls back") {
  Movie movie;
  TrackTable table(movie.source, movie.played);
  const auto add = only<command::Add>(table.select(TrackKind::Audio, 1));
  CHECK(only<command::Select>(table.trackFailed(add.key)).trackId == 1);
  CHECK(table.selected(TrackKind::Audio) == 3);
  CHECK(table.failed(1));
  CHECK(table.tracks(TrackKind::Audio).size() == 2);
  CHECK(table.select(TrackKind::Audio, 1).empty());

  const auto subtitle = only<command::Add>(table.select(TrackKind::Subtitle, 0));
  CHECK_FALSE(only<command::Select>(table.trackFailed(subtitle.key)).trackId);
  CHECK_FALSE(table.selected(TrackKind::Subtitle));
}

TEST_CASE("A transcode carries only the tracks it was opened with") {
  Movie movie(api::PlayMethod::Transcode);
  TrackTable table(movie.source, movie.played);
  const auto initial = table.selectInitial(4, 6);
  REQUIRE(initial.size() == 2);
  CHECK(std::get<command::Select>(initial[0]).trackId == 1);
  // Burned into the picture: no subtitle track in mpv.
  CHECK_FALSE(std::get<command::Select>(initial[1]).trackId);
  CHECK(table.selected(TrackKind::Subtitle) == 6);

  const auto reopen = only<command::Reopen>(table.select(TrackKind::Audio, 3));
  CHECK(reopen.audioStreamIndex == 3);
  CHECK(reopen.subtitleStreamIndex == 6);

  // External files are still added to the running transcode.
  table.select(TrackKind::Audio, 4);
  CHECK(only<command::Add>(table.select(TrackKind::Subtitle, 0)).track.kind == TrackKind::Subtitle);
  const auto off = only<command::Select>(table.select(TrackKind::Subtitle, std::nullopt));
  CHECK_FALSE(off.trackId);
}

TEST_CASE("Without a known initial audio track the first embedded one plays") {
  Movie movie;
  TrackTable table(movie.source, movie.played);
  const auto initial = table.selectInitial(77, 88);
  REQUIRE(initial.size() == 2);
  CHECK(std::get<command::Select>(initial[0]).trackId == 1);
  CHECK(table.selected(TrackKind::Audio) == 3);
  CHECK_FALSE(std::get<command::Select>(initial[1]).trackId);
  CHECK_FALSE(table.selected(TrackKind::Subtitle));
}
