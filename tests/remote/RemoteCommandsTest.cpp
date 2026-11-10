#include "../support/DoctestQt.h"

#include <jfd/remote/RemoteCommands.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The server's session messages as Jellyfin 12.2 sends them over /socket
// (MessageType and Data), read into plain values.

using namespace jfd::remote;

namespace {
  QJsonValue data(const char* json) { return QJsonDocument::fromJson(QByteArray(json)).object(); }
} // namespace

TEST_CASE("Play carries the items, the start and the chosen streams") {
  const auto parsed = parseServerMessage(QStringLiteral("Play"), data(R"({
    "ItemIds":["a1b2","c3d4","e5f6"],"StartPositionTicks":1200000000,"PlayCommand":"PlayNow",
    "ControllingUserId":"u1","StartIndex":1,"MediaSourceId":"ms1","AudioStreamIndex":2,"SubtitleStreamIndex":-1})"));
  REQUIRE(parsed.message);
  CHECK(parsed.error.isEmpty());
  const auto& play = std::get<PlayRequest>(*parsed.message);
  CHECK(play.command == PlayCommand::PlayNow);
  CHECK(play.itemIds == QStringList{QStringLiteral("a1b2"), QStringLiteral("c3d4"), QStringLiteral("e5f6")});
  CHECK(play.startIndex == 1);
  CHECK(play.startPositionTicks == 1'200'000'000);
  CHECK(play.mediaSourceId == QStringLiteral("ms1"));
  CHECK(play.audioStreamIndex == 2);
  CHECK(play.subtitleStreamIndex == -1);
}

TEST_CASE("Every PlayCommand is read; a missing one is PlayNow") {
  const std::pair<const char*, PlayCommand> commands[] = {{"PlayNow", PlayCommand::PlayNow},
                                                          {"PlayNext", PlayCommand::PlayNext},
                                                          {"PlayLast", PlayCommand::PlayLast},
                                                          {"PlayShuffle", PlayCommand::PlayShuffle},
                                                          {"PlayInstantMix", PlayCommand::PlayInstantMix}};
  for (const auto& [name, command] : commands) {
    QJsonObject object{{QStringLiteral("ItemIds"), QJsonArray{QStringLiteral("x")}},
                       {QStringLiteral("PlayCommand"), QString::fromLatin1(name)}};
    const auto parsed = parseServerMessage(QStringLiteral("Play"), object);
    REQUIRE(parsed.message);
    CHECK(std::get<PlayRequest>(*parsed.message).command == command);
  }
  const auto defaulted =
      parseServerMessage(QStringLiteral("Play"), data(R"({"ItemIds":["x"],"StartPositionTicks":null})"));
  REQUIRE(defaulted.message);
  const auto& play = std::get<PlayRequest>(*defaulted.message);
  CHECK(play.command == PlayCommand::PlayNow);
  CHECK(play.startIndex == 0);
  CHECK(play.startPositionTicks == 0);
  CHECK_FALSE(play.audioStreamIndex);
}

TEST_CASE("A Play that cannot be carried out is reported, not guessed") {
  CHECK_FALSE(parseServerMessage(QStringLiteral("Play"), data(R"({"ItemIds":[]})")).error.isEmpty());
  CHECK_FALSE(parseServerMessage(QStringLiteral("Play"), data(R"({"ItemIds":["x"],"StartIndex":3})")).error.isEmpty());
  CHECK_FALSE(parseServerMessage(QStringLiteral("Play"), data(R"({"ItemIds":["x"],"PlayCommand":"PlaySideways"})"))
                  .error.isEmpty());
  CHECK_FALSE(parseServerMessage(QStringLiteral("Play"), QJsonValue(QStringLiteral("x"))).error.isEmpty());
}

TEST_CASE("Playstate commands, with Seek's position") {
  const std::pair<const char*, PlaystateCommand> commands[] = {{"Stop", PlaystateCommand::Stop},
                                                               {"Pause", PlaystateCommand::Pause},
                                                               {"Unpause", PlaystateCommand::Unpause},
                                                               {"NextTrack", PlaystateCommand::NextTrack},
                                                               {"PreviousTrack", PlaystateCommand::PreviousTrack},
                                                               {"Rewind", PlaystateCommand::Rewind},
                                                               {"FastForward", PlaystateCommand::FastForward},
                                                               {"PlayPause", PlaystateCommand::PlayPause}};
  for (const auto& [name, command] : commands) {
    const auto parsed = parseServerMessage(QStringLiteral("Playstate"),
                                           QJsonObject{{QStringLiteral("Command"), QString::fromLatin1(name)}});
    REQUIRE(parsed.message);
    CHECK(std::get<PlaystateRequest>(*parsed.message).command == command);
  }
  const auto seek =
      parseServerMessage(QStringLiteral("Playstate"),
                         data(R"({"Command":"Seek","SeekPositionTicks":6000000000,"ControllingUserId":"u1"})"));
  REQUIRE(seek.message);
  CHECK(std::get<PlaystateRequest>(*seek.message).seekPositionTicks == 6'000'000'000);
  CHECK_FALSE(parseServerMessage(QStringLiteral("Playstate"), data(R"({"Command":"Seek"})")).error.isEmpty());
  CHECK_FALSE(parseServerMessage(QStringLiteral("Playstate"), data(R"({"Command":"Jump"})")).error.isEmpty());
}

TEST_CASE("GeneralCommand keeps its name and its arguments as text") {
  const auto parsed = parseServerMessage(
      QStringLiteral("GeneralCommand"),
      data(
          R"({"Name":"DisplayMessage","ControllingUserId":"u1","Arguments":{"Header":"Hi","Text":"Dinner","TimeoutMs":"5000"}})"));
  REQUIRE(parsed.message);
  const auto& command = std::get<GeneralCommand>(*parsed.message);
  CHECK(command.name == QStringLiteral("DisplayMessage"));
  CHECK(command.arguments.value(QStringLiteral("Header")) == QStringLiteral("Hi"));
  CHECK(command.arguments.value(QStringLiteral("TimeoutMs")) == QStringLiteral("5000"));

  // A number where the server writes text.
  const auto volume =
      parseServerMessage(QStringLiteral("GeneralCommand"), data(R"({"Name":"SetVolume","Arguments":{"Volume":40}})"));
  REQUIRE(volume.message);
  CHECK(std::get<GeneralCommand>(*volume.message).arguments.value(QStringLiteral("Volume")) == QStringLiteral("40"));
  CHECK_FALSE(parseServerMessage(QStringLiteral("GeneralCommand"), data(R"({"Arguments":{}})")).error.isEmpty());
}

TEST_CASE("Other socket messages are not remote control") {
  for (const char* type : {"ForceKeepAlive", "KeepAlive", "SyncPlayCommand", "UserDataChanged", "LibraryChanged"}) {
    const auto parsed = parseServerMessage(QString::fromLatin1(type), data(R"({"Name":"SetVolume"})"));
    CHECK_FALSE(parsed.message);
    CHECK(parsed.error.isEmpty());
  }
}

TEST_CASE("The parser knows the server's general commands") {
  for (const char* name : {"SetVolume", "SendString", "SetRepeatMode", "SetShuffleQueue", "SetMaxStreamingBitrate",
                           "PlayMediaSource", "MoveUp", "Select", "Back", "GoHome", "ToggleFullscreen"})
    CHECK(isKnownGeneralCommand(QString::fromLatin1(name)));
  CHECK_FALSE(isKnownGeneralCommand(QStringLiteral("SelfDestruct")));
}

TEST_CASE("Second-start commands") {
  using Kind = LocalCommand::Kind;
  CHECK(parseLocalCommand(QStringLiteral("play-pause"))->kind == Kind::PlayPause);
  CHECK(parseLocalCommand(QStringLiteral("Next"))->kind == Kind::Next);
  CHECK(parseLocalCommand(QStringLiteral("previous"))->kind == Kind::Previous);
  CHECK(parseLocalCommand(QStringLiteral("stop"))->kind == Kind::Stop);
  CHECK(parseLocalCommand(QStringLiteral("fullscreen"))->kind == Kind::Fullscreen);

  const auto forward = parseLocalCommand(QStringLiteral("seek:+30"));
  REQUIRE(forward);
  CHECK(forward->kind == Kind::SeekBy);
  CHECK(forward->milliseconds == 30'000);
  const auto back = parseLocalCommand(QStringLiteral("seek:-7.5"));
  REQUIRE(back);
  CHECK(back->kind == Kind::SeekBy);
  CHECK(back->milliseconds == -7'500);
  const auto to = parseLocalCommand(QStringLiteral("seek:90"));
  REQUIRE(to);
  CHECK(to->kind == Kind::SeekTo);
  CHECK(to->milliseconds == 90'000);

  for (const char* bad : {"", "tv", "play", "seek:", "seek:+", "seek:abc", "seek:+999999", "volume:50"})
    CHECK_FALSE(parseLocalCommand(QString::fromLatin1(bad)));
}

TEST_CASE("The --command value is found in a forwarded command line") {
  CHECK(commandArgument({QStringLiteral("app"), QStringLiteral("--command"), QStringLiteral("next")}) ==
        QStringLiteral("next"));
  CHECK(commandArgument({QStringLiteral("app"), QStringLiteral("--command=seek:+30")}) == QStringLiteral("seek:+30"));
  CHECK(commandArgument({QStringLiteral("app"), QStringLiteral("--command")}) == QString());
  CHECK_FALSE(commandArgument({QStringLiteral("app")}));
  // The first entry is the program, not an option.
  CHECK_FALSE(commandArgument({QStringLiteral("--command"), QStringLiteral("next")}));
}
