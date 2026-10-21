#include "support/DoctestQt.h"
#include "support/FakeServer.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/PlayerModel.h>

#include <MpvMediaBackend.h>
#include <MpvPlayer.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUrlQuery>

#include <cmath>
#include <memory>
#include <optional>

// The player screen's model on a live server (scripts/test-server) with
// headless mpv: track menus, a track change that keeps the position, a seek
// in a transcoded HLS stream, and the reports the server shows.

using namespace jfd;
using namespace jfd::api;
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

  Item findItem(JellyfinClient& client, const QString& type) {
    ItemQuery query;
    query.includeItemTypes = {type};
    auto page = run<ItemPage>([&](auto done) { client.items(query, nullptr, done); });
    REQUIRE(page.ok());
    REQUIRE_FALSE(page->items.isEmpty());
    return page->items.front();
  }

  // A signed-in client, its session socket, headless mpv and the player model.
  struct Player {
    QString deviceId;
    std::unique_ptr<JellyfinClient> client;
    std::unique_ptr<SessionSocket> socket;
    media::MpvPlayer mpv{[] {
      media::MpvPlayer::Options options;
      options.audioOutput = QStringLiteral("null");
      options.extra = {{"vo", "null"}, {"hr-seek", "yes"}};
      return options;
    }()};
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<ui::PlayerModel> model;

    Player(const QString& device, const DeviceProfileOptions& profile) : deviceId(device) {
      client = std::make_unique<JellyfinClient>(ClientIdentity{
          QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"), QStringLiteral("CI ") + device, device});
      const auto url = JellyfinClient::normalizeServerUrl(env("JFD_TEST_SERVER_URL"));
      REQUIRE_MESSAGE(url.has_value(), "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
      client->setServerUrl(*url);
      REQUIRE(run<AuthenticationResult>([&](auto done) {
                client->signIn(env("JFD_TEST_USER"), env("JFD_TEST_PASSWORD"), nullptr, done);
              }).ok());
      SessionCapabilities capabilities;
      capabilities.deviceProfile = buildDeviceProfile(profile);
      REQUIRE(run<Empty>([&](auto done) { client->reportCapabilities(capabilities, nullptr, done); }).ok());
      socket = std::make_unique<SessionSocket>(*client);
      QSignalSpy ready(socket.get(), &SessionSocket::ready);
      socket->start();
      REQUIRE(waitFor([&] { return ready.count() == 1; }, 20000));

      session::SessionRuntime::Settings settings;
      settings.deviceProfile = capabilities.deviceProfile;
      runtime = std::make_unique<session::SessionRuntime>(
          *client, *socket,
          [this](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<media::MpvMediaBackend>(mpv, clock, std::move(post));
          },
          std::move(settings));
      // No video item: mpv renders nothing, so the model need not wait for one.
      model = std::make_unique<ui::PlayerModel>(*runtime, *client, nullptr);
    }

    ~Player() {
      model->stop();
      waitFor([&] { return !model->active(); }, 5000);
      model.reset();
      runtime.reset();
    }

    // Playing marks short items played and leaves resume points; the other
    // server tests expect the library as the stand creates it. Stops first,
    // so the stop report does not mark it again.
    void restore(const QString& itemId) {
      model->stop();
      waitFor([&] { return !model->active(); }, 5000);
      waitFor([&] { return !findSession().contains(QStringLiteral("NowPlayingItem")); }, 10000);
      run<Empty>([&](auto done) { client->markUnplayed(itemId, nullptr, done); });
    }

    bool playing() const { return model->active() && !model->loading() && !model->paused(); }

    // The server's view of this device's session (GET /Sessions); empty
    // when it has none. Never throws, so destructors can use it.
    QJsonObject findSession() {
      QUrlQuery query;
      query.addQueryItem(QStringLiteral("deviceId"), deviceId);
      auto reply = run<JellyfinClient::RawResponse>([&](auto done) {
        client->send(JellyfinClient::Method::Get, QStringLiteral("/Sessions"), query, std::nullopt, nullptr, done);
      });
      if (!reply.ok()) return {};
      const QJsonArray sessions = QJsonDocument::fromJson(reply->body).array();
      return sessions.size() == 1 ? sessions.first().toObject() : QJsonObject{};
    }

    QJsonObject serverSession() {
      const QJsonObject session = findSession();
      REQUIRE_FALSE(session.isEmpty());
      return session;
    }
  };

  double serverPosition(const QJsonObject& session) {
    return session.value(QStringLiteral("PlayState")).toObject().value(QStringLiteral("PositionTicks")).toDouble() / 1e7;
  }

  int indexOfExternal(const QVariantList& tracks) {
    for (const QVariant& track : tracks) {
      const QVariantMap map = track.toMap();
      if (map.value(QStringLiteral("isExternal")).toBool()) return map.value(QStringLiteral("index")).toInt();
    }
    return -1;
  }
} // namespace

TEST_CASE("Track menus list the movie's tracks, and a change keeps the position") {
  Player p(QStringLiteral("jfd-player-tracks"), {});
  const Item movie = findItem(*p.client, QStringLiteral("Movie"));
  struct Restore {
    Player& p;
    QString id;
    ~Restore() { p.restore(id); }
  } restore{p, movie.id};

  p.model->play(movie.id, 0);
  REQUIRE(waitFor([&] { return p.playing() && p.model->position() > 3; }, 60000));

  // Two audio tracks; an embedded and an external subtitle.
  const QVariantList audio = p.model->audioTracks();
  const QVariantList subtitles = p.model->subtitleTracks();
  REQUIRE(audio.size() == 2);
  REQUIRE(subtitles.size() == 2);
  const int external = indexOfExternal(subtitles);
  REQUIRE(external >= 0);

  // Paused, so positions can be compared exactly.
  p.model->togglePause();
  REQUIRE(waitFor([&] { return p.model->paused(); }, 10000));
  const double before = p.model->position();

  // mpv plays the track the menu marks.
  const auto language = [&](int index) {
    for (const QVariant& track : audio)
      if (track.toMap().value(QStringLiteral("index")).toInt() == index)
        return track.toMap().value(QStringLiteral("language")).toString();
    return QString();
  };
  const auto mpvLanguage = [&] { return p.mpv.property("current-tracks/audio/lang").toString(); };
  const int first = p.model->audioIndex();
  REQUIRE(first >= 0);
  CHECK(waitFor([&] { return mpvLanguage() == language(first); }, 10000));

  // The other audio track, at the same position.
  int second = -1;
  for (const QVariant& track : audio) {
    const int index = track.toMap().value(QStringLiteral("index")).toInt();
    if (index != first) second = index;
  }
  REQUIRE(second >= 0);
  p.model->selectAudio(second);
  REQUIRE(waitFor([&] { return p.model->audioIndex() == second && !p.model->loading(); }, 30000));
  // mpv switches on its own thread.
  CHECK(waitFor([&] { return mpvLanguage() == language(second); }, 10000));
  CHECK(p.model->paused());
  CHECK(std::abs(p.model->position() - before) < 1.0);

  // The external subtitle.
  p.model->selectSubtitle(external);
  REQUIRE(waitFor([&] { return p.model->subtitleIndex() == external && !p.model->loading(); }, 30000));
  REQUIRE(waitFor([&] { return p.mpv.property("current-tracks/sub/external").toBool(); }, 10000));
  CHECK(std::abs(p.model->position() - before) < 1.0);

  // Subtitles off.
  p.model->selectSubtitle(-1);
  REQUIRE(waitFor([&] { return p.model->subtitleIndex() < 0; }, 30000));

  // The server shows the item and the chosen tracks.
  REQUIRE(waitFor(
      [&] {
        const QJsonObject session = p.serverSession();
        const QJsonObject state = session.value(QStringLiteral("PlayState")).toObject();
        return session.value(QStringLiteral("NowPlayingItem")).toObject().value(QStringLiteral("Id")).toString() ==
                   movie.id &&
               state.value(QStringLiteral("AudioStreamIndex")).toInt(-1) == second;
      },
      20000));
}

TEST_CASE("A seek in a transcoded HLS stream lands within a second, and the server shows it") {
  DeviceProfileOptions profile;
  profile.alwaysForceTranscode = true;
  Player p(QStringLiteral("jfd-player-hls"), profile);
  const Item episode = findItem(*p.client, QStringLiteral("Episode"));
  struct Restore {
    Player& p;
    QString id;
    ~Restore() { p.restore(id); }
  } restore{p, episode.id};

  p.model->play(episode.id, 0);
  REQUIRE(waitFor([&] { return p.playing() && p.model->position() > 2; }, 90000));
  CHECK(waitFor(
      [&] {
        return p.serverSession().value(QStringLiteral("PlayState")).toObject().value(QStringLiteral("PlayMethod")).toString() ==
               QStringLiteral("Transcode");
      },
      20000));

  // Paused, so the position stays put while it is compared.
  p.model->togglePause();
  REQUIRE(waitFor([&] { return p.model->paused(); }, 10000));
  constexpr double Target = 200.0;
  p.model->seek(Target);
  REQUIRE(waitFor([&] { return std::abs(p.model->position() - Target) < 1.0 && !p.model->loading(); }, 60000));
  // mpv's own clock agrees: the HLS segments map to the item's timeline.
  REQUIRE(waitFor([&] { return std::abs(p.mpv.property("time-pos").toDouble() - Target) < 1.0; }, 10000));
  CHECK(std::abs(p.model->position() - Target) < 1.0);
  REQUIRE(waitFor([&] { return std::abs(serverPosition(p.serverSession()) - Target) < 1.0; }, 20000));

  // Playing on from there.
  p.model->togglePause();
  REQUIRE(waitFor([&] { return p.playing() && p.model->position() > Target + 2; }, 30000));

  // Stopping is reported: the server keeps the resume point near the seek.
  p.model->stop();
  REQUIRE(waitFor([&] { return !p.model->active(); }, 10000));
  REQUIRE(waitFor([&] { return !p.serverSession().contains(QStringLiteral("NowPlayingItem")); }, 20000));
  auto item = run<Item>([&](auto done) { p.client->item(episode.id, nullptr, done); });
  REQUIRE(item.ok());
  CHECK(std::abs(static_cast<double>(item->userData.playbackPositionTicks) / 1e7 - (Target + 2)) < 5.0);
}
