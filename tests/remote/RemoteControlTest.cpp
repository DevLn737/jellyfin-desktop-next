#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/platform/SingleInstance.h>
#include <jfd/remote/RemoteControl.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUuid>

#include <atomic>
#include <thread>
#include <vector>

// The remote control on fake server messages: what reaches the session (as
// the media controls' intents) and what reaches the application's actions.

using namespace jfd;
using namespace jfd::remote;
using core::MediaTicks;
using core::PlaybackPhase;
using core::SessionInput;
using core::SessionSnapshot;
using Outcome = RemoteControl::Outcome;
namespace intent = core::intent;

namespace {

  QJsonValue data(const char* json) { return QJsonDocument::fromJson(QByteArray(json)).object(); }

  SessionSnapshot playing(PlaybackPhase phase = PlaybackPhase::Playing) {
    SessionSnapshot snapshot;
    snapshot.phase = phase;
    snapshot.item = core::ItemId("movie-1");
    snapshot.title = "A Movie";
    snapshot.position = *MediaTicks::fromMilliseconds(65'000);
    snapshot.duration = MediaTicks::fromMilliseconds(1'800'000);
    return snapshot;
  }

  SessionSnapshot inGroup(PlaybackPhase phase = PlaybackPhase::Playing) {
    SessionSnapshot snapshot = playing(phase);
    snapshot.group = core::GroupSnapshot{};
    snapshot.group->group = core::GroupId("g1");
    snapshot.group->participants = {"alice", "bob"};
    return snapshot;
  }

  // Records every action the application would carry out.
  struct Recorder {
    QStringList calls;
    int volume = 50;
    bool muted = false;
    std::vector<PlayRequest> plays;
    std::vector<PlayRequest> groupPlays;

    RemoteActions all() {
      RemoteActions actions = withoutNavigation();
      actions.navigate = [this](NavigationKey key) { calls << QStringLiteral("navigate %1").arg(int(key)); };
      actions.sendText = [this](const QString& text) { calls << QStringLiteral("text ") + text; };
      return actions;
    }

    // What the application connects while the shell has no input router.
    RemoteActions withoutNavigation() {
      RemoteActions actions;
      actions.play = [this](const PlayRequest& request) { plays.push_back(request); };
      actions.playInGroup = [this](const PlayRequest& request) { groupPlays.push_back(request); };
      actions.volume = [this] { return volume; };
      actions.setVolume = [this](int value) {
        volume = value;
        calls << QStringLiteral("volume %1").arg(value);
      };
      actions.muted = [this] { return muted; };
      actions.setMuted = [this](bool value) {
        muted = value;
        calls << QStringLiteral("muted %1").arg(value ? 1 : 0);
      };
      actions.selectAudio = [this](int index) { calls << QStringLiteral("audio %1").arg(index); };
      actions.selectSubtitle = [this](int index) { calls << QStringLiteral("subtitle %1").arg(index); };
      actions.showMessage = [this](const QString& header, const QString& text) {
        calls << QStringLiteral("message %1|%2").arg(header, text);
      };
      actions.toggleFullscreen = [this] { calls << QStringLiteral("fullscreen"); };
      actions.goTo = [this](Destination destination) { calls << QStringLiteral("go %1").arg(int(destination)); };
      actions.setRepeatMode = [this](RepeatMode mode) { calls << QStringLiteral("repeat %1").arg(int(mode)); };
      actions.setShuffle = [this](bool on) { calls << QStringLiteral("shuffle %1").arg(on ? 1 : 0); };
      actions.setMaxBitrate = [this](qint64 bitrate) { calls << QStringLiteral("bitrate %1").arg(bitrate); };
      return actions;
    }
  };

  struct Fixture {
    SessionSnapshot snapshot = playing();
    std::vector<SessionInput> posted;
    Recorder recorder;
    std::unique_ptr<RemoteControl> remote;

    explicit Fixture(bool navigation = false) {
      remote = std::make_unique<RemoteControl>([this](SessionInput input) { posted.push_back(std::move(input)); },
                                               [this] { return snapshot; },
                                               navigation ? recorder.all() : recorder.withoutNavigation());
    }

    Outcome playstate(const char* json) { return remote->handle(QStringLiteral("Playstate"), data(json)); }
    Outcome general(const char* json) { return remote->handle(QStringLiteral("GeneralCommand"), data(json)); }

    template <class T> const T* last() const { return posted.empty() ? nullptr : std::get_if<T>(&posted.back()); }
  };

  // Arguments that make each general command do something.
  QJsonObject argumentsFor(const QString& name) {
    if (name == QLatin1String("SetVolume")) return {{QStringLiteral("Volume"), QStringLiteral("30")}};
    if (name == QLatin1String("SetAudioStreamIndex")) return {{QStringLiteral("Index"), QStringLiteral("2")}};
    if (name == QLatin1String("SetSubtitleStreamIndex")) return {{QStringLiteral("Index"), QStringLiteral("-1")}};
    if (name == QLatin1String("DisplayMessage"))
      return {{QStringLiteral("Header"), QStringLiteral("Hi")}, {QStringLiteral("Text"), QStringLiteral("Dinner")}};
    if (name == QLatin1String("SendString")) return {{QStringLiteral("String"), QStringLiteral("dune")}};
    if (name == QLatin1String("SetRepeatMode")) return {{QStringLiteral("RepeatMode"), QStringLiteral("RepeatAll")}};
    if (name == QLatin1String("SetShuffleQueue")) return {{QStringLiteral("ShuffleMode"), QStringLiteral("Shuffle")}};
    if (name == QLatin1String("SetMaxStreamingBitrate"))
      return {{QStringLiteral("MaxBitrate"), QStringLiteral("10000000")}};
    return {};
  }

} // namespace

TEST_CASE("Playstate reaches the session as the media controls' intents") {
  Fixture f;
  CHECK(f.playstate(R"({"Command":"Pause"})") == Outcome::Done);
  CHECK(f.last<intent::Pause>());
  CHECK(f.playstate(R"({"Command":"Unpause"})") == Outcome::Done);
  CHECK(f.last<intent::Play>());
  CHECK(f.playstate(R"({"Command":"Stop"})") == Outcome::Done);
  CHECK(f.last<intent::Stop>());
  CHECK(f.playstate(R"({"Command":"NextTrack"})") == Outcome::Done);
  CHECK(f.last<intent::Next>());
  CHECK(f.playstate(R"({"Command":"PreviousTrack"})") == Outcome::Done);
  CHECK(f.last<intent::Previous>());

  // PlayPause follows what plays, like the media key.
  CHECK(f.playstate(R"({"Command":"PlayPause"})") == Outcome::Done);
  CHECK(f.last<intent::Pause>());
  f.snapshot.phase = PlaybackPhase::Paused;
  f.playstate(R"({"Command":"PlayPause"})");
  CHECK(f.last<intent::Play>());
  // Nothing open: nothing to toggle.
  f.snapshot = SessionSnapshot{};
  const auto before = f.posted.size();
  f.playstate(R"({"Command":"PlayPause"})");
  CHECK(f.posted.size() == before);
}

TEST_CASE("Seek, Rewind and FastForward move within the file") {
  Fixture f;
  f.playstate(R"({"Command":"Seek","SeekPositionTicks":6000000000})");
  REQUIRE(f.last<intent::Seek>());
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 600'000);
  // Past the end: to the end.
  f.playstate(R"({"Command":"Seek","SeekPositionTicks":99000000000})");
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 1'800'000);

  f.playstate(R"({"Command":"Rewind"})");
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 65'000 - RemoteControl::RewindMs);
  f.playstate(R"({"Command":"FastForward"})");
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 65'000 + RemoteControl::FastForwardMs);
  f.snapshot.position = *MediaTicks::fromMilliseconds(4'000);
  f.playstate(R"({"Command":"Rewind"})");
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 0);

  // Nothing open: no seek.
  f.snapshot = SessionSnapshot{};
  const auto before = f.posted.size();
  f.playstate(R"({"Command":"Seek","SeekPositionTicks":10000000})");
  CHECK(f.posted.size() == before);
}

TEST_CASE("In a group, Playstate posts the same intents, which the session sends to the group") {
  Fixture f;
  f.snapshot = inGroup();
  f.playstate(R"({"Command":"Pause"})");
  CHECK(f.last<intent::Pause>());
  f.playstate(R"({"Command":"Seek","SeekPositionTicks":3000000000})");
  REQUIRE(f.last<intent::Seek>());
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 300'000);
  f.playstate(R"({"Command":"NextTrack"})");
  CHECK(f.last<intent::Next>());
}

TEST_CASE("Play goes to the launcher alone and to the group's queue in a group") {
  Fixture f;
  f.snapshot = SessionSnapshot{};
  CHECK(f.remote->handle(QStringLiteral("Play"), data(R"({"ItemIds":["a","b"],"StartIndex":1,
      "StartPositionTicks":50000000,"PlayCommand":"PlayNow","AudioStreamIndex":3})")) == Outcome::Done);
  REQUIRE(f.recorder.plays.size() == 1);
  CHECK(f.recorder.plays.front().itemIds == QStringList{QStringLiteral("a"), QStringLiteral("b")});
  CHECK(f.recorder.plays.front().startIndex == 1);
  CHECK(f.recorder.plays.front().startPositionTicks == 50'000'000);
  CHECK(f.recorder.plays.front().audioStreamIndex == 3);
  CHECK(f.recorder.groupPlays.empty());

  f.snapshot = inGroup();
  CHECK(f.remote->handle(QStringLiteral("Play"), data(R"({"ItemIds":["c"],"PlayCommand":"PlayNext"})")) ==
        Outcome::Done);
  REQUIRE(f.recorder.groupPlays.size() == 1);
  CHECK(f.recorder.groupPlays.front().command == PlayCommand::PlayNext);
  CHECK(f.recorder.plays.size() == 1);

  // Instant mixes are not made here.
  CHECK(f.remote->handle(QStringLiteral("Play"), data(R"({"ItemIds":["c"],"PlayCommand":"PlayInstantMix"})")) ==
        Outcome::Unsupported);
  CHECK(f.recorder.groupPlays.size() == 1);
}

TEST_CASE("Every advertised command has a handler that acts; the rest are ignored") {
  Fixture f;
  const QStringList supported = f.remote->supportedCommands();
  CHECK_FALSE(supported.isEmpty());
  CHECK(supported.contains(QStringLiteral("SetVolume")));
  CHECK(supported.contains(QStringLiteral("DisplayMessage")));
  CHECK(supported.contains(QStringLiteral("GoHome")));
  // No input router yet: navigation is not offered.
  CHECK_FALSE(supported.contains(QStringLiteral("MoveUp")));
  CHECK_FALSE(supported.contains(QStringLiteral("SendString")));
  for (const QString& name : supported) {
    CAPTURE(name);
    CHECK(isKnownGeneralCommand(name));
    const auto calls = f.recorder.calls.size();
    QJsonObject command{{QStringLiteral("Name"), name}, {QStringLiteral("Arguments"), argumentsFor(name)}};
    CHECK(f.remote->handle(QStringLiteral("GeneralCommand"), command) == Outcome::Done);
    CHECK(f.recorder.calls.size() == calls + 1);
  }

  // Known but not advertised, and unknown: nothing happens.
  const auto calls = f.recorder.calls.size();
  for (const QString& name : knownGeneralCommands()) {
    if (supported.contains(name)) continue;
    CAPTURE(name);
    QJsonObject command{{QStringLiteral("Name"), name}, {QStringLiteral("Arguments"), argumentsFor(name)}};
    CHECK(f.remote->handle(QStringLiteral("GeneralCommand"), command) == Outcome::Unsupported);
  }
  CHECK(f.general(R"({"Name":"SelfDestruct"})") == Outcome::Unsupported);
  CHECK(f.recorder.calls.size() == calls);
}

TEST_CASE("Navigation is offered once the application connects it") {
  Fixture f(true);
  const QStringList supported = f.remote->supportedCommands();
  for (const char* name : {"MoveUp", "MoveDown", "MoveLeft", "MoveRight", "Select", "Back", "SendString"})
    CHECK(supported.contains(QString::fromLatin1(name)));
  CHECK(f.general(R"({"Name":"MoveLeft"})") == Outcome::Done);
  CHECK(f.recorder.calls.back() == QStringLiteral("navigate %1").arg(int(NavigationKey::Left)));
}

TEST_CASE("General commands carry their arguments") {
  Fixture f;
  f.general(R"({"Name":"SetVolume","Arguments":{"Volume":"140"}})");
  CHECK(f.recorder.volume == 100);
  f.recorder.volume = 98;
  f.general(R"({"Name":"VolumeUp"})");
  CHECK(f.recorder.volume == 100);
  f.general(R"({"Name":"VolumeDown"})");
  CHECK(f.recorder.volume == 100 - RemoteControl::VolumeStep);
  f.general(R"({"Name":"ToggleMute"})");
  CHECK(f.recorder.muted);
  f.general(R"({"Name":"Unmute"})");
  CHECK_FALSE(f.recorder.muted);
  f.general(R"({"Name":"SetSubtitleStreamIndex","Arguments":{"Index":"4"}})");
  CHECK(f.recorder.calls.back() == QStringLiteral("subtitle 4"));
  f.general(R"({"Name":"DisplayMessage","Arguments":{"Header":"","Text":"Pizza is here","TimeoutMs":"5000"}})");
  CHECK(f.recorder.calls.back() == QStringLiteral("message |Pizza is here"));
  f.general(R"({"Name":"GoToSettings"})");
  CHECK(f.recorder.calls.back() == QStringLiteral("go %1").arg(int(Destination::Settings)));
  f.general(R"({"Name":"SetRepeatMode","Arguments":{"RepeatMode":"RepeatOne"}})");
  CHECK(f.recorder.calls.back() == QStringLiteral("repeat %1").arg(int(RepeatMode::One)));
  f.general(R"({"Name":"SetShuffleQueue","Arguments":{"ShuffleMode":"Sorted"}})");
  CHECK(f.recorder.calls.back() == QStringLiteral("shuffle 0"));

  // Unusable arguments change nothing.
  const auto calls = f.recorder.calls.size();
  CHECK(f.general(R"({"Name":"SetVolume","Arguments":{"Volume":"loud"}})") == Outcome::Malformed);
  CHECK(f.general(R"({"Name":"SetAudioStreamIndex","Arguments":{}})") == Outcome::Malformed);
  CHECK(f.general(R"({"Name":"SetSubtitleStreamIndex","Arguments":{"Index":"-2"}})") == Outcome::Malformed);
  CHECK(f.general(R"({"Name":"DisplayMessage","Arguments":{"Header":" "}})") == Outcome::Malformed);
  CHECK(f.general(R"({"Name":"SetRepeatMode","Arguments":{"RepeatMode":"Sometimes"}})") == Outcome::Malformed);
  CHECK(f.recorder.calls.size() == calls);
}

TEST_CASE("Turned off, the client says so and ignores the server") {
  Fixture f;
  api::SessionCapabilities capabilities;
  f.remote->applyTo(capabilities);
  CHECK(capabilities.supportsMediaControl);
  CHECK(capabilities.supportedCommands == f.remote->supportedCommands());

  f.remote->setEnabled(false);
  f.remote->applyTo(capabilities);
  CHECK_FALSE(capabilities.supportsMediaControl);
  CHECK(capabilities.supportedCommands.isEmpty());

  CHECK(f.playstate(R"({"Command":"Pause"})") == Outcome::Disabled);
  CHECK(f.general(R"({"Name":"SetVolume","Arguments":{"Volume":"10"}})") == Outcome::Disabled);
  CHECK(f.remote->handle(QStringLiteral("Play"), data(R"({"ItemIds":["a"]})")) == Outcome::Disabled);
  CHECK(f.posted.empty());
  CHECK(f.recorder.calls.isEmpty());
  CHECK(f.recorder.plays.empty());

  // The computer's own --command still works.
  CHECK(f.remote->run(LocalCommand{LocalCommand::Kind::PlayPause, 0}) == Outcome::Done);
  CHECK(f.last<intent::Pause>());
}

TEST_CASE("Second-start commands go the same way") {
  Fixture f;
  using Kind = LocalCommand::Kind;
  f.remote->run({Kind::PlayPause, 0});
  CHECK(f.last<intent::Pause>());
  f.remote->run({Kind::Next, 0});
  CHECK(f.last<intent::Next>());
  f.remote->run({Kind::Previous, 0});
  CHECK(f.last<intent::Previous>());
  f.remote->run({Kind::SeekBy, 30'000});
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 95'000);
  f.remote->run({Kind::SeekTo, 10'000});
  CHECK(f.last<intent::Seek>()->position.milliseconds() == 10'000);
  f.remote->run({Kind::Stop, 0});
  CHECK(f.last<intent::Stop>());
  CHECK(f.remote->run({Kind::Fullscreen, 0}) == Outcome::Done);
  CHECK(f.recorder.calls.back() == QStringLiteral("fullscreen"));
}

TEST_CASE("Messages from a fake server's socket are carried out") {
  test::FakeServer server{[](const test::HttpRequest&) { return test::HttpResponse{204, {}, {}, 0}; }};
  QList<QWebSocket*> sockets;
  server.onWebSocket([&](QWebSocket* socket) { sockets.append(socket); });
  api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"), QStringLiteral("CI"),
                                                 QStringLiteral("jfd-remote")}};
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
  api::SessionSocket socket(client);

  Fixture f;
  QObject::connect(
      &socket, &api::SessionSocket::messageReceived, f.remote.get(),
      [&f](const QString& type, const QJsonValue& value, const QString&) { f.remote->handle(type, value); });
  QSignalSpy ready(&socket, &api::SessionSocket::ready);
  socket.start();
  REQUIRE(test::waitFor([&] { return ready.count() == 1 && !sockets.isEmpty(); }));

  QWebSocket* from = sockets.front();
  from->sendTextMessage(QStringLiteral(
      R"({"MessageType":"Playstate","MessageId":"m1","Data":{"Command":"Pause","ControllingUserId":"u2"}})"));
  from->sendTextMessage(QStringLiteral(
      R"({"MessageType":"GeneralCommand","MessageId":"m2","Data":{"Name":"SetVolume","ControllingUserId":"u2","Arguments":{"Volume":"25"}}})"));
  from->sendTextMessage(QStringLiteral(
      R"({"MessageType":"Play","MessageId":"m3","Data":{"ItemIds":["e1"],"StartPositionTicks":0,"PlayCommand":"PlayNow","ControllingUserId":"u2"}})"));
  REQUIRE(test::waitFor([&] { return !f.recorder.plays.empty(); }));
  REQUIRE(f.posted.size() == 1);
  CHECK(std::holds_alternative<intent::Pause>(f.posted.front()));
  CHECK(f.recorder.volume == 25);
  CHECK(f.recorder.plays.front().itemIds == QStringList{QStringLiteral("e1")});
  socket.stop();
}

TEST_CASE("A second start's --command reaches the running app's remote control") {
  const QString key = QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  platform::SingleInstance first(key);
  REQUIRE(first.start({QStringLiteral("app")}));

  Fixture f;
  QObject::connect(&first, &platform::SingleInstance::activated, f.remote.get(), [&f](const QStringList& arguments) {
    if (const auto text = commandArgument(arguments))
      if (const auto command = parseLocalCommand(*text)) f.remote->run(*command);
  });

  // The second start runs on another thread, like another process: its
  // blocking hand-over needs this event loop.
  std::atomic<int> sent{-1};
  std::thread second([&] {
    platform::SingleInstance instance(key);
    sent = instance.sendToPrimary({QStringLiteral("app"), QStringLiteral("--command"), QStringLiteral("play-pause")})
               ? 1
               : 0;
  });
  REQUIRE(test::waitFor([&] { return sent != -1 && !f.posted.empty(); }));
  second.join();
  CHECK(sent == 1);
  CHECK(std::holds_alternative<intent::Pause>(f.posted.front()));

  // With nothing running, the command has nowhere to go.
  platform::SingleInstance alone(QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
  CHECK_FALSE(alone.sendToPrimary({QStringLiteral("app"), QStringLiteral("--command"), QStringLiteral("next")}, 200));
  CHECK_FALSE(alone.isPrimary());
}
