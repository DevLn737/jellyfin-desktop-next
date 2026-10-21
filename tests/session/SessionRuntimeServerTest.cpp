#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/api/SyncPlayApi.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>

#ifdef JFD_SESSION_TEST_MPV
#include "MpvMediaBackend.h"
#include "MpvPlayer.h"
#endif

#include <QSignalSpy>

#include <memory>
#include <optional>

// The session runtime on a live server (scripts/test-server): our client
// creates a group, plays the group's item and follows another member. Built
// with a virtual backend, and with headless mpv when the app is built
// (JFD_SESSION_TEST_MPV), which then plays the server's real stream.

using namespace jfd;
using namespace jfd::api;
using jfd::session::SessionRuntime;
using jfd::test::waitFor;

namespace {
  QString env(const char* name) { return qEnvironmentVariable(name); }

  template <class T> Result<T> run(std::function<void(Callback<T>)> call, int timeoutMs = 20000) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    if (!waitFor([&] { return out.has_value(); }, timeoutMs)) {
      ApiError error;
      error.kind = ApiError::Kind::Timeout;
      return error;
    }
    return std::move(*out);
  }

  std::unique_ptr<JellyfinClient> signedIn(const QString& deviceId) {
    auto client = std::make_unique<JellyfinClient>(
        ClientIdentity{QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"),
                       QStringLiteral("CI ") + deviceId, deviceId});
    const auto url = JellyfinClient::normalizeServerUrl(env("JFD_TEST_SERVER_URL"));
    REQUIRE_MESSAGE(url.has_value(), "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
    client->setServerUrl(*url);
    auto auth = run<AuthenticationResult>([&](auto done) {
      client->signIn(env("JFD_TEST_USER"), env("JFD_TEST_PASSWORD"), nullptr, done);
    });
    REQUIRE(auth.ok());
    return client;
  }

  Item findItem(JellyfinClient& client, const QString& type) {
    ItemQuery query;
    query.includeItemTypes = {type};
    auto page = run<ItemPage>([&](auto done) { client.items(query, nullptr, done); });
    REQUIRE(page.ok());
    REQUIRE_FALSE(page->items.isEmpty());
    return page->items.front();
  }

  SessionCapabilities capabilities() {
    SessionCapabilities c;
    c.supportsSyncPlayPlaybackRate = true;
    c.deviceProfile = buildDeviceProfile({});
    return c;
  }

  // A signed-in client with its session socket ready.
  struct Connection {
    std::unique_ptr<JellyfinClient> client;
    std::unique_ptr<SessionSocket> socket;
    std::unique_ptr<SyncPlayApi> syncPlay;

    explicit Connection(const QString& deviceId) : client(signedIn(deviceId)) {
      REQUIRE(run<Empty>([&](auto done) { client->reportCapabilities(capabilities(), nullptr, done); }).ok());
      socket = std::make_unique<SessionSocket>(*client);
      syncPlay = std::make_unique<SyncPlayApi>(*client);
      QSignalSpy ready(socket.get(), &SessionSocket::ready);
      socket->start();
      REQUIRE(waitFor([&] { return ready.count() == 1; }, 20000));
    }
  };

#ifdef JFD_SESSION_TEST_MPV
  struct Backend {
    media::MpvPlayer player{[] {
      media::MpvPlayer::Options options;
      options.audioOutput = QStringLiteral("null");
      options.extra = {{"vo", "null"}, {"hr-seek", "yes"}};
      return options;
    }()};
    SessionRuntime::BackendFactory factory() {
      return [this](const core::Clock& clock, SessionRuntime::Post post) {
        return std::make_unique<media::MpvMediaBackend>(player, clock, std::move(post));
      };
    }
  };
#else
  struct Backend {
    SessionRuntime::BackendFactory factory() {
      return [](const core::Clock& clock, SessionRuntime::Post post) {
        return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
      };
    }
  };
#endif

  // The virtual and mpv builds may run at the same time: separate sessions.
  QString device(const char* name) {
#ifdef JFD_SESSION_TEST_MPV
    return QStringLiteral("jfd-runtime-mpv-") + QLatin1String(name);
#else
    return QStringLiteral("jfd-runtime-") + QLatin1String(name);
#endif
  }

  bool contains(const std::vector<std::string>& names, const QString& name) {
    return std::find(names.begin(), names.end(), name.toStdString()) != names.end();
  }
} // namespace

TEST_CASE("Our client creates a group, plays its item and follows another member") {
  Connection host(device("host"));
  Connection guest(device("guest"));
  const Item movie = findItem(*host.client, QStringLiteral("Movie"));
  const Item episode = findItem(*host.client, QStringLiteral("Episode"));

  Backend backend;
  SessionRuntime::Settings settings;
  settings.deviceProfile = buildDeviceProfile({});
  SessionRuntime runtime(*host.client, *host.socket, backend.factory(), settings);
  core::SessionSnapshot latest;
  auto subscription = runtime.hub().subscribe([&](const core::SessionSnapshot& s) { latest = s; });
  const auto& user = env("JFD_TEST_USER");

  runtime.createGroup(device("group"));
  REQUIRE(waitFor([&] { return latest.group.has_value(); }, 20000));
  CHECK(latest.group->name == device("group").toStdString());
  CHECK(contains(latest.group->participants, user));
  const QString groupId = QString::fromStdString(latest.group->group.value());

  // The group's item reaches the launcher, which opens the server's stream.
  REQUIRE(run<Empty>([&](auto done) { host.syncPlay->setNewQueue({movie.id, episode.id}, 0, 0, nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return latest.item.value() == movie.id.toStdString() &&
                               latest.phase == core::PlaybackPhase::Paused; }, 30000));
  CHECK(latest.title == movie.name.toStdString());

  // Play through the session: the group unpauses everyone, us included.
  runtime.post(core::intent::Play{});
  REQUIRE(waitFor([&] { return latest.phase == core::PlaybackPhase::Playing &&
                               latest.group->state == core::GroupState::Playing; }, 20000));
  REQUIRE(waitFor([&] { return latest.position.milliseconds() > 2'000 && latest.group->driftMilliseconds; }, 20000));
  MESSAGE("drift while playing: " << *latest.group->driftMilliseconds << " ms");
  CHECK(std::abs(*latest.group->driftMilliseconds) < 400);

  // Another member joins (without holding the group back) and leaves.
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->joinGroup(groupId, nullptr, done); }).ok());
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->setIgnoreWait(true, nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return latest.group && latest.group->participants.size() == 2; }, 20000));

  // A seek by the other member moves us too.
  const std::int64_t target = 15'000;
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->seek(target * 10'000, nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return latest.phase == core::PlaybackPhase::Playing &&
                               latest.position.milliseconds() >= target &&
                               latest.position.milliseconds() < target + 5'000; }, 20000));

  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->leaveGroup(nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return latest.group && latest.group->participants.size() == 1; }, 20000));

  // Next from the media keys moves the group to the episode.
  runtime.post(core::intent::Next{});
  REQUIRE(waitFor([&] { return latest.item.value() == episode.id.toStdString() && latest.phase != core::PlaybackPhase::Loading; }, 30000));
  CHECK(latest.title == (episode.seriesName + QStringLiteral(" – ") + episode.name).toStdString());

  bool left = false;
  runtime.leaveBeforeExit([&] { left = true; });
  REQUIRE(waitFor([&] { return left; }, 5000));
  REQUIRE(waitFor([&] { return !latest.group.has_value(); }, 10000));
}

TEST_CASE("Playing alone resolves the item and reports Next to the app's queue") {
  Connection host(device("solo"));
  const Item movie = findItem(*host.client, QStringLiteral("Movie"));
  Backend backend;
  SessionRuntime::Settings settings;
  settings.deviceProfile = buildDeviceProfile({});
  SessionRuntime runtime(*host.client, *host.socket, backend.factory(), settings);
  QSignalSpy steps(&runtime, &SessionRuntime::queueStepRequested);

  runtime.play(movie.id, 5'000 * 10'000);
  REQUIRE(waitFor([&] { return runtime.snapshot().phase == core::PlaybackPhase::Playing &&
                               runtime.snapshot().position.milliseconds() >= 5'000; }, 30000));
  CHECK_FALSE(runtime.snapshot().group);
  runtime.post(core::intent::Next{});
  REQUIRE(waitFor([&] { return steps.count() == 1; }, 5000));
  CHECK(steps.front().front().toBool());
}

TEST_CASE("After the computer sleeps our client reconnects and rejoins its group") {
  Connection host(device("sleep-host"));
  Connection guest(device("sleep-guest"));
  const Item movie = findItem(*host.client, QStringLiteral("Movie"));
  Backend backend;
  SessionRuntime::Settings settings;
  settings.deviceProfile = buildDeviceProfile({});
  SessionRuntime runtime(*host.client, *host.socket, backend.factory(), settings);
  core::SessionSnapshot latest;
  auto subscription = runtime.hub().subscribe([&](const core::SessionSnapshot& s) { latest = s; });
  QSignalSpy ready(host.socket.get(), &SessionSocket::ready);

  runtime.createGroup(device("sleep-group"));
  REQUIRE(waitFor([&] { return latest.group.has_value(); }, 20000));
  const QString groupId = QString::fromStdString(latest.group->group.value());
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->joinGroup(groupId, nullptr, done); }).ok());
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->setIgnoreWait(true, nullptr, done); }).ok());
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->setNewQueue({movie.id}, 0, 0, nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return latest.phase == core::PlaybackPhase::Paused; }, 30000));
  runtime.post(core::intent::Play{});
  REQUIRE(waitFor([&] { return latest.phase == core::PlaybackPhase::Playing &&
                               latest.position.milliseconds() > 2'000; }, 20000));

  // The group moves on while we sleep; on waking we reconnect and follow.
  // (The test clip is 30 s long; the server keeps seeks inside it.)
  runtime.resumeAfterSleep();
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->seek(20'000 * 10'000, nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return ready.count() == 1; }, 20000));
  CHECK(ready.front().front().toULongLong() > 1);
  const bool followed = waitFor([&] { return latest.group && latest.phase == core::PlaybackPhase::Playing &&
                                             latest.position.milliseconds() >= 20'000; }, 30000);
  INFO("phase " << static_cast<int>(latest.phase) << ", position " << latest.position.milliseconds()
                << " ms, in group " << latest.group.has_value() << ", participants "
                << (latest.group ? latest.group->participants.size() : 0));
  REQUIRE(followed);

  bool left = false;
  runtime.leaveBeforeExit([&] { left = true; });
  REQUIRE(waitFor([&] { return left; }, 5000));
}

#ifndef JFD_SESSION_TEST_MPV
namespace {
  bool forkServer() { return env("JFD_TEST_SERVER_FORK") == QLatin1String("1"); }

  bool noticed(const QSignalSpy& spy, const QString& part) {
    for (const auto& arguments : spy)
      if (arguments.front().toString().contains(part)) return true;
    return false;
  }
} // namespace

TEST_CASE("The group model creates, lists, joins, changes speed and leaves groups") {
  Connection host(device("model-host"));
  Connection guest(device("model-guest"));
  const Item movie = findItem(*host.client, QStringLiteral("Movie"));
  Backend backend;
  SessionRuntime::Settings settings;
  settings.deviceProfile = buildDeviceProfile({});
  SessionRuntime runtime(*host.client, *host.socket, backend.factory(), settings);
  session::GroupModel model(runtime, *host.client, *host.socket);
  QSignalSpy notices(&model, &session::GroupModel::notice);
  const QString user = env("JFD_TEST_USER");

  // The playback-rate extension is found on the fork and not on stock 12.2.
  if (forkServer())
    REQUIRE(waitFor([&] { return model.rateSupported(); }, 10000));
  else
    CHECK_FALSE(waitFor([&] { return model.rateSupported(); }, 2000));

  // Create a group; it shows in the server's list.
  model.createGroup(device("model-group"));
  REQUIRE(waitFor([&] { return model.inGroup() && !model.busy(); }, 20000));
  CHECK(model.groupName() == device("model-group"));
  CHECK(model.participants().contains(user));
  model.refreshGroups();
  REQUIRE(waitFor([&] { return !model.busy(); }, 10000));
  bool listed = false;
  for (const QVariant& group : model.groups())
    listed = listed || group.toMap().value(QStringLiteral("id")).toString() == model.groupId();
  CHECK(listed);

  // Another member joins: the model says so and shows the queue they start.
  const QString groupId = model.groupId();
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->joinGroup(groupId, nullptr, done); }).ok());
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->setIgnoreWait(true, nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return model.participants().size() == 2; }, 20000));
  CHECK(noticed(notices, QStringLiteral("joined the group")));
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->setNewQueue({movie.id}, 0, 0, nullptr, done); }).ok());
  REQUIRE(waitFor([&] {
    return model.queue().size() == 1 &&
           model.queue().front().toMap().value(QStringLiteral("name")).toString() == movie.name;
  }, 20000));
  CHECK(model.queue().front().toMap().value(QStringLiteral("current")).toBool());

  // Speed: changed for everyone on the fork, refused up front on stock.
  model.setRate(1.5);
  if (forkServer()) {
    REQUIRE(waitFor([&] { return model.rate() == doctest::Approx(1.5); }, 20000));
    CHECK(noticed(notices, QStringLiteral("Group speed")));
  } else {
    CHECK(noticed(notices, QStringLiteral("cannot change the group's speed")));
    CHECK(model.rate() == doctest::Approx(1.0));
  }

  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->leaveGroup(nullptr, done); }).ok());
  REQUIRE(waitFor([&] { return model.participants().size() == 1; }, 20000));
  CHECK(noticed(notices, QStringLiteral("left the group")));
  model.leaveGroup();
  REQUIRE(waitFor([&] { return !model.inGroup() && !model.busy(); }, 20000));
  CHECK(model.queue().isEmpty());

  // Joining a group someone else made, and one that does not exist.
  auto made = run<SyncPlayGroup>([&](auto done) {
    guest.syncPlay->createGroup(device("model-guest-group"), nullptr, done);
  });
  REQUIRE(made.ok());
  model.joinGroup(made->groupId);
  REQUIRE(waitFor([&] { return model.inGroup() && !model.busy(); }, 20000));
  CHECK(model.groupId() == made->groupId);
  model.leaveGroup();
  REQUIRE(waitFor([&] { return !model.inGroup(); }, 20000));
  REQUIRE(run<Empty>([&](auto done) { guest.syncPlay->leaveGroup(nullptr, done); }).ok());

  model.joinGroup(QStringLiteral("00000000000000000000000000000000"));
  REQUIRE(waitFor([&] { return !model.busy(); }, 20000));
  CHECK_FALSE(model.inGroup());
  CHECK_FALSE(model.errorText().isEmpty());
}
#endif
