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
#include <QImage>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUrlQuery>

#include <cmath>
#include <memory>
#include <optional>

// The player screen's model on a live server (scripts/test-server) with
// headless mpv: track menus, a track change that keeps the position, a seek
// in a transcoded HLS stream, the reports the server shows, the next
// episode and the seek bar thumbnails. On the extras stand
// (extras-server.sh, JFD_EXTRAS_*; skipped without it): an external audio
// file, and skipping intros and credits.

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

    // `stand`: the environment prefix, JFD_TEST (stock-server.sh) or
    // JFD_EXTRAS (extras-server.sh).
    Player(const QString& device, const DeviceProfileOptions& profile, const QString& stand = QStringLiteral("JFD_TEST"))
        : deviceId(device) {
      client = std::make_unique<JellyfinClient>(ClientIdentity{
          QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"), QStringLiteral("CI ") + device, device});
      const auto var = [&](const char* name) { return qEnvironmentVariable(qPrintable(stand + QLatin1String(name))); };
      const auto url = JellyfinClient::normalizeServerUrl(var("_SERVER_URL"));
      REQUIRE_MESSAGE(url.has_value(), "Set " << stand.toStdString() << "_SERVER_URL (scripts/test-server)");
      client->setServerUrl(*url);
      REQUIRE(run<AuthenticationResult>([&](auto done) {
                client->signIn(var("_USER"), var("_PASSWORD"), nullptr, done);
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

TEST_CASE("At the end of an episode the next one plays, and Previous goes back") {
  Player p(QStringLiteral("jfd-player-queue"), {});
  ItemQuery query;
  query.includeItemTypes = {QStringLiteral("Episode")};
  query.sortBy = QStringLiteral("SortName");
  auto page = run<ItemPage>([&](auto done) { p.client->items(query, nullptr, done); });
  REQUIRE(page.ok());
  REQUIRE(page->items.size() == 2);
  const QString first = page->items[0].id;
  const QString second = page->items[1].id;
  struct Restore {
    Player& p;
    QStringList ids;
    ~Restore() {
      for (const QString& id : ids) p.restore(id);
    }
  } restore{p, {first, second}};

  // A few seconds before the end of the first episode.
  const auto duration = page->items[0].runTimeTicks.value_or(0);
  REQUIRE(duration > 10 * 10'000'000LL);
  p.model->play(first, duration - 3 * 10'000'000LL);
  REQUIRE(waitFor([&] { return p.playing(); }, 60000));
  REQUIRE(waitFor([&] { return p.model->hasNext(); }, 10000));
  CHECK_FALSE(p.model->hasPrevious());

  const bool moved = waitFor([&] { return p.runtime->snapshot().item.value() == second.toStdString() && p.playing(); }, 60000);
  REQUIRE(moved);
  CHECK(p.model->position() < 10);
  REQUIRE(waitFor([&] { return p.model->hasPrevious(); }, 10000));
  CHECK_FALSE(p.model->hasNext());

  p.model->previous();
  REQUIRE(waitFor([&] { return p.runtime->snapshot().item.value() == first.toStdString() && p.playing(); }, 60000));
}

TEST_CASE("The seek bar has the server's trickplay thumbnails") {
  Player p(QStringLiteral("jfd-player-trickplay"), {});
  const Item episode = findItem(*p.client, QStringLiteral("Episode"));
  struct Restore {
    Player& p;
    QString id;
    ~Restore() { p.restore(id); }
  } restore{p, episode.id};

  p.model->play(episode.id, 0);
  REQUIRE(waitFor([&] { return p.playing(); }, 60000));
  REQUIRE(waitFor([&] { return p.model->hasTrickplay(); }, 20000));

  // The thumbnail ten seconds in, inside its tile.
  const QVariantMap frame = p.model->trickplayFrame(10);
  const QString prefix = QStringLiteral("image://jfd/");
  const QString source = frame.value(QStringLiteral("source")).toString();
  REQUIRE(source.startsWith(prefix));
  const int width = frame.value(QStringLiteral("width")).toInt();
  const int height = frame.value(QStringLiteral("height")).toInt();
  const int tileWidth = frame.value(QStringLiteral("tileWidth")).toInt();
  const int tileHeight = frame.value(QStringLiteral("tileHeight")).toInt();
  CHECK(width > 0);
  CHECK(height > 0);
  CHECK(frame.value(QStringLiteral("x")).toInt() + width <= tileWidth);
  CHECK(frame.value(QStringLiteral("y")).toInt() + height <= tileHeight);

  // The tile behind it loads with the session's authorization, and holds
  // the thumbnail's rectangle.
  const QUrl tile(QUrl::fromPercentEncoding(source.mid(prefix.size()).toLatin1()));
  auto response = run<JellyfinClient::RawResponse>(
      [&](auto done) { p.client->authorizedGet(tile, nullptr, done); });
  REQUIRE(response.ok());
  CHECK(response->httpStatus == 200);
  const QImage image = QImage::fromData(response->body);
  REQUIRE_FALSE(image.isNull());
  CHECK(image.width() >= frame.value(QStringLiteral("x")).toInt() + width);
  CHECK(image.height() >= frame.value(QStringLiteral("y")).toInt() + height);

  // A later position in the same tile moves to another thumbnail.
  const QVariantMap later = p.model->trickplayFrame(20);
  CHECK(later.value(QStringLiteral("source")) == source);
  CHECK(later.value(QStringLiteral("x")) != frame.value(QStringLiteral("x")));
}

namespace {
  bool hasExtrasStand() {
    if (!env("JFD_EXTRAS_SERVER_URL").isEmpty()) return true;
    MESSAGE("JFD_EXTRAS_SERVER_URL is not set (scripts/test-server/extras-server.sh up): skipped");
    return false;
  }
} // namespace

TEST_CASE("An external audio file plays, through a transcode, at the same position") {
  if (!hasExtrasStand()) return;
  Player p(QStringLiteral("jfd-player-external-audio"), {}, QStringLiteral("JFD_EXTRAS"));
  const Item movie = findItem(*p.client, QStringLiteral("Movie"));
  struct Restore {
    Player& p;
    QString id;
    ~Restore() { p.restore(id); }
  } restore{p, movie.id};

  p.model->play(movie.id, 0);
  REQUIRE(waitFor([&] { return p.playing(); }, 60000));
  // The clock starts at zero although the transcode's timestamps do not.
  CHECK(p.model->position() < 5);
  const QVariantList audio = p.model->audioTracks();
  REQUIRE(audio.size() == 2);
  const int external = indexOfExternal(audio);
  REQUIRE(external >= 0);
  int embedded = -1;
  for (const QVariant& track : audio) {
    const int index = track.toMap().value(QStringLiteral("index")).toInt();
    if (index != external) embedded = index;
  }
  REQUIRE(embedded >= 0);
  REQUIRE(waitFor([&] { return p.model->position() > 3; }, 30000));

  p.model->togglePause();
  REQUIRE(waitFor([&] { return p.model->paused(); }, 10000));
  const double before = p.model->position();
  const auto playState = [&] { return p.serverSession().value(QStringLiteral("PlayState")).toObject(); };

  // Each audio track in turn. The server mixes an external file into a
  // transcode, so mpv sees one audio track either way; what plays is the
  // server's choice, and the server shows it.
  for (const int index : {embedded, external}) {
    if (p.model->audioIndex() != index) p.model->selectAudio(index);
    REQUIRE(waitFor([&] { return p.model->audioIndex() == index && !p.model->loading(); }, 30000));
    CHECK(p.model->paused());
    CHECK(std::abs(p.model->position() - before) < 1.0);
    REQUIRE(waitFor([&] { return playState().value(QStringLiteral("AudioStreamIndex")).toInt(-1) == index; }, 20000));
  }
  CHECK(playState().value(QStringLiteral("PlayMethod")).toString() == QStringLiteral("Transcode"));
}

TEST_CASE("The intro shows a Skip button that jumps past it; credits skip on their own") {
  if (!hasExtrasStand()) return;
  Player p(QStringLiteral("jfd-player-segments"), {}, QStringLiteral("JFD_EXTRAS"));
  const Item movie = findItem(*p.client, QStringLiteral("Movie"));
  struct Restore {
    Player& p;
    QString id;
    ~Restore() { p.restore(id); }
  } restore{p, movie.id};

  // The stand's segments: intro 2-12 s, credits 50-58 s.
  p.model->play(movie.id, 0);
  REQUIRE(waitFor([&] { return p.playing(); }, 60000));
  REQUIRE(waitFor([&] { return p.model->segment() == QStringLiteral("Intro"); }, 20000));
  CHECK(p.model->position() >= 1.5);
  CHECK(p.model->position() < 12);
  p.model->skipSegment();
  REQUIRE(waitFor([&] { return p.model->position() >= 11.9 && !p.model->loading(); }, 20000));
  CHECK(p.model->position() < 15);
  CHECK(waitFor([&] { return p.model->segment().isEmpty(); }, 5000));

  // With auto-skip on, playback jumps over the credits: no position well
  // inside them is ever shown.
  p.model->setAutoSkip(true);
  p.model->seek(48);
  REQUIRE(waitFor([&] { return std::abs(p.model->position() - 48) < 1.0 && !p.model->loading(); }, 20000));
  bool insideCredits = false;
  QObject::connect(p.model.get(), &ui::PlayerModel::positionChanged, p.model.get(), [&] {
    const double position = p.model->position();
    if (position > 51 && position < 57) insideCredits = true;
  });
  REQUIRE(waitFor([&] { return p.model->position() >= 57.9 || p.model->ended(); }, 30000));
  CHECK_FALSE(insideCredits);
}
