#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/MusicApi.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/music/MusicPlayer.h>
#include <jfd/session/SessionRuntime.h>

#include <MpvMediaBackend.h>
#include <MpvPlayer.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <optional>

// The music player on the extras stand (scripts/test-server/extras-server.sh,
// JFD_EXTRAS_*; skipped without it) with headless mpv: the queue plays
// through the session, each track is reported to the server, a video takes
// the session from music and the tracks of an album meet without a gap.

using namespace jfd;
using namespace jfd::api;
using jfd::test::waitFor;

namespace {
  QString env(const char* name) { return qEnvironmentVariable(name); }

  bool hasExtrasStand() {
    if (!env("JFD_EXTRAS_SERVER_URL").isEmpty()) return true;
    MESSAGE("JFD_EXTRAS_SERVER_URL is not set (scripts/test-server/extras-server.sh up): skipped");
    return false;
  }

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

  // A signed-in client, headless mpv and the music player on the session.
  struct Stand {
    std::unique_ptr<JellyfinClient> client;
    std::unique_ptr<SessionSocket> socket;
    std::unique_ptr<media::MpvPlayer> mpv;
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<music::MusicPlayer> player;
    std::unique_ptr<MusicApi> api;
    QStringList touched;

    // `pcmFile`: the audio goes to this file as 48 kHz mono s16le, as fast as
    // it decodes, instead of nowhere.
    explicit Stand(const QString& device, const QString& pcmFile = {}) {
      client = std::make_unique<JellyfinClient>(ClientIdentity{
          QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"), QStringLiteral("CI ") + device, device});
      const auto url = JellyfinClient::normalizeServerUrl(env("JFD_EXTRAS_SERVER_URL"));
      REQUIRE(url.has_value());
      client->setServerUrl(*url);
      REQUIRE(run<AuthenticationResult>([&](auto done) {
                client->signIn(env("JFD_EXTRAS_USER"), env("JFD_EXTRAS_PASSWORD"), nullptr, done);
              }).ok());
      SessionCapabilities capabilities;
      capabilities.deviceProfile = buildDeviceProfile({});
      REQUIRE(run<Empty>([&](auto done) { client->reportCapabilities(capabilities, nullptr, done); }).ok());
      socket = std::make_unique<SessionSocket>(*client);
      QSignalSpy ready(socket.get(), &SessionSocket::ready);
      socket->start();
      REQUIRE(waitFor([&] { return ready.count() == 1; }, 20000));

      media::MpvPlayer::Options options;
      options.audioOutput = pcmFile.isEmpty() ? QStringLiteral("null") : QStringLiteral("pcm");
      options.extra = {{"vo", "null"}};
      if (!pcmFile.isEmpty()) {
        QFile::remove(pcmFile);
        options.extra += {{"ao-pcm-file", pcmFile.toUtf8()},
                          {"ao-pcm-waveheader", "no"},
                          {"ao-pcm-append", "yes"},
                          {"audio-format", "s16"},
                          {"audio-channels", "mono"},
                          {"audio-samplerate", "48000"}};
      }
      mpv = std::make_unique<media::MpvPlayer>(options);
      session::SessionRuntime::Settings settings;
      settings.deviceProfile = capabilities.deviceProfile;
      runtime = std::make_unique<session::SessionRuntime>(
          *client, *socket,
          [this](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<media::MpvMediaBackend>(*mpv, clock, std::move(post));
          },
          std::move(settings));
      player = std::make_unique<music::MusicPlayer>(*runtime, *client);
      api = std::make_unique<MusicApi>(*client);
    }

    ~Stand() {
      if (player) {
        player->stop();
        waitFor([&] { return !player->active(); }, 5000);
      }
      player.reset();
      runtime.reset();
    }

    Item album(const QString& name) {
      MusicQuery query;
      query.searchTerm = name;
      auto page = run<ItemPage>([&](auto done) { api->albums(query, nullptr, done); });
      REQUIRE(page.ok());
      REQUIRE_FALSE(page->items.isEmpty());
      return page->items.front();
    }

    QList<Item> tracksOf(const Item& album) {
      auto page = run<ItemPage>([&](auto done) { api->albumTracks(album.id, nullptr, done); });
      REQUIRE(page.ok());
      touched += [&] {
        QStringList ids;
        for (const Item& track : page->items)
          ids.append(track.id);
        return ids;
      }();
      return page->items;
    }

    // Jellyfin marks short items played when their playback stops, and the
    // other tests expect the library as the stand makes it: mark again
    // until it stays (the server saves after it clears the session).
    void unplay(const QStringList& ids) {
      waitFor([&] { return !playerSessionPlaying(); }, 10000);
      for (const QString& id : ids) {
        for (int attempt = 0; attempt < 5; ++attempt) {
          run<Empty>([&](auto done) { client->markUnplayed(id, nullptr, done); });
          const auto clean = [&] {
            auto item = run<Item>([&](auto done) { client->item(id, nullptr, done); });
            return item.ok() && !item->userData.played && item->userData.playCount == 0;
          };
          if (!waitFor([&] { return !clean(); }, 1500)) break;
        }
      }
    }

    bool playerSessionPlaying() { return player && player->active(); }

    bool played(const QString& id) {
      auto item = run<Item>([&](auto done) { client->item(id, nullptr, done); });
      return item.ok() && (item->userData.played || item->userData.playCount > 0);
    }

    void seekNearEnd(double secondsLeft) {
      REQUIRE(waitFor([&] { return player->duration() > secondsLeft + 1; }, 10000));
      player->seek(player->duration() - secondsLeft);
    }
  };

  struct Cleanup {
    Stand& stand;
    ~Cleanup() { stand.unplay(stand.touched); }
  };
} // namespace

TEST_CASE("An album plays through in order; each track is reported when it ends") {
  if (!hasExtrasStand()) return;
  Stand s(QStringLiteral("jfd-music-album"));
  Cleanup cleanup{s};
  const Item album = s.album(QStringLiteral("First Album"));
  const QList<Item> tracks = s.tracksOf(album);
  REQUIRE(tracks.size() == 3);

  s.player->playAlbum(album.id);
  REQUIRE(waitFor([&] { return s.player->playing(); }, 60000));
  CHECK(s.player->currentIndex() == 0);
  CHECK(s.player->queueSize() == 3);
  CHECK(s.player->currentTrack().value(QStringLiteral("title")).toString() == QStringLiteral("Alpha One"));
  CHECK(s.player->hasNext());
  CHECK_FALSE(s.player->hasPrevious());

  for (int index = 1; index < 3; ++index) {
    REQUIRE(waitFor([&] { return s.player->nextQueued(); }, 20000));
    s.seekNearEnd(2);
    const bool advanced = waitFor([&] { return s.player->currentIndex() == index && s.player->playing(); }, 30000);
    if (!advanced)
      MESSAGE("album step " << index << ": index " << s.player->currentIndex() << " playing " << s.player->playing()
                            << " loading " << s.player->loading() << " active " << s.player->active() << " position "
                            << s.player->position() << " handovers " << s.player->handoverCount() << " opens "
                            << s.player->openCount() << " error " << s.player->error().toStdString());
    REQUIRE(advanced);
    // The previous track played to its end: the server shows it played.
    CHECK(waitFor([&] { return s.played(tracks[index - 1].id); }, 20000));
  }
  CHECK_FALSE(s.player->hasNext());

  // The last track ends: the queue plays out, nothing more starts.
  s.seekNearEnd(2);
  REQUIRE(waitFor([&] { return !s.player->active(); }, 30000));
  CHECK(s.player->currentIndex() == 2);
  CHECK(s.player->queueSize() == 3);
  CHECK(waitFor([&] { return s.played(tracks[2].id); }, 20000));
}

TEST_CASE("Next, previous, repeat and the queue's edits") {
  if (!hasExtrasStand()) return;
  Stand s(QStringLiteral("jfd-music-controls"));
  Cleanup cleanup{s};
  const Item album = s.album(QStringLiteral("First Album"));
  const QList<Item> tracks = s.tracksOf(album);
  REQUIRE(tracks.size() == 3);

  s.player->playAlbum(album.id);
  REQUIRE(waitFor([&] { return s.player->playing(); }, 60000));
  s.player->next();
  REQUIRE(waitFor([&] { return s.player->currentIndex() == 1 && s.player->playing(); }, 30000));
  s.player->next();
  REQUIRE(waitFor([&] { return s.player->currentIndex() == 2 && s.player->playing(); }, 30000));

  // Past the first seconds Previous restarts the track; then it goes back.
  REQUIRE(waitFor([&] { return s.player->position() > 3.5; }, 20000));
  s.player->previous();
  REQUIRE(waitFor([&] { return s.player->position() < 2.0; }, 10000));
  CHECK(s.player->currentIndex() == 2);
  s.player->previous();
  REQUIRE(waitFor([&] { return s.player->currentIndex() == 1 && s.player->playing(); }, 30000));

  // Pause and unpause.
  s.player->togglePause();
  REQUIRE(waitFor([&] { return s.player->paused(); }, 10000));
  s.player->togglePause();
  REQUIRE(waitFor([&] { return s.player->playing(); }, 10000));

  // Repeat the queue: the last track goes back to the first.
  s.player->setRepeat(QStringLiteral("queue"));
  s.player->jumpTo(2);
  REQUIRE(waitFor([&] { return s.player->currentIndex() == 2 && s.player->playing(); }, 30000));
  CHECK(s.player->hasNext());
  // The wrap is queued behind the track before it ends, as it is in use.
  REQUIRE(waitFor([&] { return s.player->nextQueued(); }, 20000));
  s.seekNearEnd(2);
  const bool wrapped = waitFor([&] { return s.player->currentIndex() == 0 && s.player->playing(); }, 30000);
  if (!wrapped)
    MESSAGE("repeat wrap: index " << s.player->currentIndex() << " playing " << s.player->playing() << " loading "
                                   << s.player->loading() << " active " << s.player->active() << " position "
                                   << s.player->position() << " handovers " << s.player->handoverCount() << " opens "
                                   << s.player->openCount() << " error " << s.player->error().toStdString());
  REQUIRE(wrapped);
  s.player->setRepeat(QStringLiteral("none"));

  // Removing the playing track plays the one that followed it.
  s.player->removeAt(0);
  REQUIRE(waitFor([&] { return s.player->queueSize() == 2 && s.player->playing(); }, 30000));
  CHECK(s.player->currentTrack().value(QStringLiteral("title")).toString() == QStringLiteral("Alpha Two"));

  // Clearing stops it.
  s.player->clear();
  REQUIRE(waitFor([&] { return !s.player->active(); }, 10000));
  CHECK(s.player->queueSize() == 0);
}

TEST_CASE("A video takes the session from music; the queue stays and the track's report ends") {
  if (!hasExtrasStand()) return;
  Stand s(QStringLiteral("jfd-music-video"));
  Cleanup cleanup{s};
  const Item album = s.album(QStringLiteral("First Album"));
  const QList<Item> tracks = s.tracksOf(album);
  ItemQuery query;
  query.includeItemTypes = {QStringLiteral("Movie")};
  auto movies = run<ItemPage>([&](auto done) { s.client->items(query, nullptr, done); });
  REQUIRE(movies.ok());
  REQUIRE_FALSE(movies->items.isEmpty());
  const QString movieId = movies->items.front().id;
  s.touched.append(movieId);

  s.player->playAlbum(album.id, 1);
  REQUIRE(waitFor([&] { return s.player->playing(); }, 60000));
  CHECK(s.player->currentIndex() == 1);
  REQUIRE(waitFor([&] { return s.player->position() > 2; }, 20000));
  CHECK_FALSE(s.player->videoActive());

  s.runtime->play(movieId, 0);
  REQUIRE(waitFor([&] { return s.player->videoActive(); }, 60000));
  CHECK_FALSE(s.player->active());
  CHECK(s.player->queueSize() == 3);
  CHECK(s.player->currentIndex() == 1);
  // The track was replaced, not finished: its stop report arrived.
  CHECK(waitFor([&] { return s.played(tracks[1].id); }, 20000));

  // And the player does not step when the video ends or Next arrives.
  s.runtime->post(core::intent::Stop{});
  REQUIRE(waitFor([&] { return !s.player->videoActive(); }, 20000));
  CHECK_FALSE(s.player->active());
  CHECK(s.player->currentIndex() == 1);
}

namespace {
  // 48 kHz mono s16le; a sine whose period is a whole number of samples
  // satisfies x[n+1] = 2cos(w)x[n] - x[n-1], so a seam that drops, repeats
  // or shifts samples shows as a large residual.
  struct SineCheck {
    qint64 samples = 0;
    int worstResidual = 0;
    qint64 worstAt = -1;
  };

  SineCheck checkSine(const QByteArray& pcm, double frequency) {
    SineCheck out;
    const auto* data = reinterpret_cast<const qint16*>(pcm.constData());
    out.samples = pcm.size() / 2;
    const double c = 2.0 * std::cos(2.0 * M_PI * frequency / 48000.0);
    for (qint64 n = 1; n + 1 < out.samples; ++n) {
      const double predicted = c * data[n] - data[n - 1];
      const int residual = static_cast<int>(std::abs(predicted - data[n + 1]));
      if (residual > out.worstResidual) {
        out.worstResidual = residual;
        out.worstAt = n;
      }
    }
    return out;
  }
} // namespace

TEST_CASE("The tracks of a continuous album meet without a gap") {
  if (!hasExtrasStand()) return;
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString pcmFile = dir.filePath(QStringLiteral("out.pcm"));
  Stand s(QStringLiteral("jfd-music-gapless"), pcmFile);
  Cleanup cleanup{s};
  const Item album = s.album(QStringLiteral("Tone Album"));
  const QList<Item> tracks = s.tracksOf(album);
  REQUIRE(tracks.size() == 3);

  QElapsedTimer timer;
  timer.start();
  s.player->playAlbum(album.id);
  QStringList log;
  int seen = -1;
  REQUIRE(waitFor(
      [&] {
        if (s.player->currentIndex() != seen) {
          seen = s.player->currentIndex();
          log << QStringLiteral("track %1 at %2 ms, %3 bytes written").arg(seen).arg(timer.elapsed()).arg(QFileInfo(pcmFile).size());
        }
        return seen == 2;
      },
      120000));
  // The last track plays out.
  // The output is untimed, so a track plays in a blink: wait for the end.
  QElapsedTimer progress;
  progress.start();
  REQUIRE(waitFor(
      [&] {
        if (progress.elapsed() > 3000) {
          progress.restart();
          qInfo() << "music state: index" << s.player->currentIndex() << "active" << s.player->active() << "loading"
                  << s.player->loading() << "handovers" << s.player->handoverCount() << "opens" << s.player->openCount();
        }
        return !s.player->active() && !s.player->loading() && s.player->currentIndex() == 2;
      },
      120000));
  INFO("handovers " << s.player->handoverCount() << ", opens " << s.player->openCount());
  // The backend moved on by itself at least once (the output is untimed, so
  // on a slow machine a track can end before the next is queued behind it).
  CHECK(s.player->handoverCount() >= 1);
  log << QStringLiteral("end at %1 ms, %2 bytes written").arg(timer.elapsed()).arg(QFileInfo(pcmFile).size());
  INFO(log.join(QStringLiteral("; ")).toStdString());
  // The audio output closes with the player.
  s.player.reset();
  s.runtime.reset();
  s.mpv.reset();

  QFile file(pcmFile);
  REQUIRE(file.open(QIODevice::ReadOnly));
  const QByteArray pcm = file.readAll();
  const SineCheck result = checkSine(pcm, 1000.0);
  // Three 240 s tracks.
  const qint64 expected = 3 * 240 * 48000;
  INFO("samples " << result.samples << " of " << expected << ", worst residual " << result.worstResidual << " at "
                  << result.worstAt);
  // Within 50 ms of the audio there should be, and unbroken at the seams.
  CHECK(std::abs(result.samples - expected) < 48 * 50);
  CHECK(result.worstResidual < 200);
}
