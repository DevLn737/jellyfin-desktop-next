#include "../support/DoctestQt.h"

#include <jfd/playback/TrackPreference.h>

using namespace jfd;
using namespace jfd::playback;
using core::TrackKind;

namespace {

  api::MediaStream stream(int index, const char* type, const char* language, bool external = false,
                          const char* title = "", bool forced = false) {
    api::MediaStream s;
    s.index = index;
    s.type = QString::fromLatin1(type);
    s.language = QString::fromLatin1(language);
    s.title = QString::fromLatin1(title);
    s.isExternal = external;
    s.isForced = forced;
    return s;
  }

  // An episode: Japanese and Russian audio, an English and two Russian
  // subtitles (signs only, full), an external Russian audio file.
  api::MediaSource episode(int offset = 0) {
    api::MediaSource source;
    source.id = QStringLiteral("source");
    source.mediaStreams = {stream(0, "Video", ""),
                           stream(1 + offset, "Audio", "jpn"),
                           stream(2 + offset, "Audio", "rus", false, "Dub"),
                           stream(3 + offset, "Subtitle", "eng"),
                           stream(4 + offset, "Subtitle", "rus", false, "Signs", true),
                           stream(5 + offset, "Subtitle", "rus", false, "Full"),
                           stream(6 + offset, "Audio", "rus", true, "Studio")};
    source.defaultAudioStreamIndex = 1 + offset;
    source.defaultSubtitleStreamIndex = 3 + offset;
    return source;
  }

  struct Chosen {
    std::optional<int> audio;
    std::optional<int> subtitle;
  };

  Chosen choose(const TrackPreference& preference, const api::MediaSource& source,
                Chosen request = {}) {
    preference.choose(source, request.audio, request.subtitle);
    return request;
  }

} // namespace

TEST_CASE("Nothing is chosen before the viewer picks") {
  TrackPreference preference;
  const Chosen chosen = choose(preference, episode());
  CHECK_FALSE(chosen.audio);
  CHECK_FALSE(chosen.subtitle);
}

TEST_CASE("The picked language carries to the next episode, whatever its indices") {
  TrackPreference preference;
  preference.remember(TrackKind::Audio, episode(), 2);
  preference.remember(TrackKind::Subtitle, episode(), 5);
  const Chosen chosen = choose(preference, episode(10));
  CHECK(chosen.audio == 12);
  CHECK(chosen.subtitle == 15);
}

TEST_CASE("External and forced tracks are matched as such") {
  TrackPreference preference;
  preference.remember(TrackKind::Audio, episode(), 6);
  preference.remember(TrackKind::Subtitle, episode(), 4);
  const Chosen chosen = choose(preference, episode(10));
  CHECK(chosen.audio == 16);
  CHECK(chosen.subtitle == 14);

  // Without an external file the embedded track in that language is next best.
  api::MediaSource embeddedOnly = episode(10);
  embeddedOnly.mediaStreams.removeLast();
  CHECK(choose(preference, embeddedOnly).audio == 12);
}

TEST_CASE("Subtitles turned off stay off") {
  TrackPreference preference;
  preference.remember(TrackKind::Subtitle, episode(), std::nullopt);
  CHECK(choose(preference, episode()).subtitle == -1);

  api::MediaSource noDefault = episode();
  noDefault.defaultSubtitleStreamIndex = -1;
  CHECK_FALSE(choose(preference, noDefault).subtitle);
}

TEST_CASE("The source's default and missing languages are left to the server") {
  TrackPreference preference;
  preference.remember(TrackKind::Audio, episode(), 1);
  preference.remember(TrackKind::Subtitle, episode(), 3);
  Chosen chosen = choose(preference, episode());
  CHECK_FALSE(chosen.audio);
  CHECK_FALSE(chosen.subtitle);

  preference.remember(TrackKind::Audio, episode(), 2);
  api::MediaSource japaneseOnly = episode();
  japaneseOnly.mediaStreams = {stream(0, "Video", ""), stream(1, "Audio", "jpn")};
  CHECK_FALSE(choose(preference, japaneseOnly).audio);
}

TEST_CASE("Indices the request carries are kept") {
  TrackPreference preference;
  preference.remember(TrackKind::Audio, episode(), 2);
  preference.remember(TrackKind::Subtitle, episode(), std::nullopt);
  const Chosen chosen = choose(preference, episode(), {1, 5});
  CHECK(chosen.audio == 1);
  CHECK(chosen.subtitle == 5);
}

TEST_CASE("An unknown index changes nothing") {
  TrackPreference preference;
  preference.remember(TrackKind::Audio, episode(), 2);
  preference.remember(TrackKind::Audio, episode(), 42);
  preference.remember(TrackKind::Subtitle, episode(), 42);
  const Chosen chosen = choose(preference, episode(10));
  CHECK(chosen.audio == 12);
  CHECK_FALSE(chosen.subtitle);
}

TEST_CASE("Language codes match in their short, bibliographic and terminology forms") {
  CHECK(sameLanguage(QStringLiteral("ger"), QStringLiteral("deu")));
  CHECK(sameLanguage(QStringLiteral("de"), QStringLiteral("GER")));
  CHECK(sameLanguage(QStringLiteral("rus"), QStringLiteral("ru")));
  CHECK_FALSE(sameLanguage(QStringLiteral("rus"), QStringLiteral("ukr")));
  CHECK_FALSE(sameLanguage(QString(), QString()));
}

TEST_CASE("Before any pick, the preferred languages and the subtitle mode decide") {
  TrackPreference preference;
  TrackDefaults defaults;
  defaults.audioLanguage = QStringLiteral("ru");
  defaults.subtitleLanguage = QStringLiteral("rus");
  defaults.subtitleMode = SubtitleMode::Always;
  preference.setDefaults(defaults);
  // The Russian dub (embedded, flagged default among Russian ones: none, so
  // the first), and full Russian subtitles rather than signs.
  Chosen chosen = choose(preference, episode());
  CHECK(chosen.audio == 2);
  CHECK(chosen.subtitle == 5);

  defaults.subtitleMode = SubtitleMode::OnlyForced;
  preference.setDefaults(defaults);
  CHECK(choose(preference, episode()).subtitle == 4);
  api::MediaSource noForced = episode();
  noForced.mediaStreams.removeAt(4);
  CHECK(choose(preference, noForced).subtitle == -1);

  defaults.subtitleMode = SubtitleMode::Off;
  preference.setDefaults(defaults);
  CHECK(choose(preference, episode()).subtitle == -1);

  defaults.subtitleMode = SubtitleMode::Default;
  preference.setDefaults(defaults);
  CHECK_FALSE(choose(preference, episode()).subtitle);

  // A language the file lacks leaves the server's choice.
  defaults.audioLanguage = QStringLiteral("fre");
  defaults.subtitleLanguage = QStringLiteral("fre");
  defaults.subtitleMode = SubtitleMode::Always;
  preference.setDefaults(defaults);
  chosen = choose(preference, episode());
  CHECK_FALSE(chosen.audio);
  CHECK_FALSE(chosen.subtitle);
}

TEST_CASE("The viewer's pick wins over the defaults") {
  TrackPreference preference;
  TrackDefaults defaults;
  defaults.audioLanguage = QStringLiteral("rus");
  defaults.subtitleMode = SubtitleMode::Off;
  preference.setDefaults(defaults);
  preference.remember(TrackKind::Audio, episode(), 1);
  preference.remember(TrackKind::Subtitle, episode(), 3);
  const Chosen chosen = choose(preference, episode(10));
  CHECK_FALSE(chosen.audio);  // Japanese is the default there already.
  CHECK_FALSE(chosen.subtitle);  // So is English.
}
