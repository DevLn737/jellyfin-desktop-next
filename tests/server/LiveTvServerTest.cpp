#include "../support/DoctestQt.h"
#include "../support/ExtrasStand.h"
#include "../support/FakeServer.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/LiveTv.h>
#include <jfd/library/LiveTvChannels.h>
#include <jfd/library/LiveTvGuide.h>
#include <jfd/library/LiveTvOnNow.h>
#include <jfd/library/LiveTvSchedule.h>
#include <jfd/library/ItemListModel.h>

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>
#include <memory>
#include <optional>

// Live TV on the extras stand (scripts/test-server/extras-server.sh: an M3U
// tuner of 50 channels and a 24-hour XMLTV guide; JFD_EXTRAS_*, skipped
// without it): Info, the channel list with now and next, the guide grid's
// model, the home screen's "On now" row, a channel's page, a channel's
// PlaybackInfo, its stream through the server, and the stream closed once
// Stopped names it.

using namespace jfd;
using namespace jfd::api;
using jfd::test::hasExtrasStand;
using jfd::test::waitFor;

namespace {
  QString env(const char* name) { return qEnvironmentVariable(name); }

  int expectedChannels() {
    bool ok = false;
    const int n = env("JFD_EXTRAS_LIVETV_CHANNELS").toInt(&ok);
    return ok ? n : 50;
  }

  template <class T> Result<T> run(std::function<void(Callback<T>)> call, int timeoutMs = 30000) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    if (!waitFor([&] { return out.has_value(); }, timeoutMs)) {
      ApiError error;
      error.kind = ApiError::Kind::Timeout;
      return error;
    }
    return std::move(*out);
  }

  struct Stand {
    std::unique_ptr<JellyfinClient> client;
    QNetworkAccessManager network;

    Stand() {
      client = std::make_unique<JellyfinClient>(
          ClientIdentity{QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"),
                         QStringLiteral("CI livetv"), QStringLiteral("jfd-livetv-test")});
      const auto url = JellyfinClient::normalizeServerUrl(env("JFD_EXTRAS_SERVER_URL"));
      REQUIRE(url.has_value());
      client->setServerUrl(*url);
      REQUIRE(run<AuthenticationResult>([&](auto done) {
                client->signIn(env("JFD_EXTRAS_USER"), env("JFD_EXTRAS_PASSWORD"), nullptr, done);
              }).ok());
    }

    ~Stand() {
      run<Empty>([&](auto done) { client->signOut(nullptr, done); }, 5000);
    }

    // The status of a GET on the server, or 0 when nothing came back in
    // time; with `firstBytes`, it waits only for the first bytes of a body
    // that never ends (a live stream).
    int status(const QUrl& url, qint64* firstByteMs = nullptr, int timeoutMs = 10000) {
      QNetworkRequest request(url);
      request.setRawHeader("Authorization", client->authorizationHeader());
      QElapsedTimer clock;
      clock.start();
      QNetworkReply* reply = network.get(request);
      int code = 0;
      bool done = false;
      QObject::connect(reply, &QNetworkReply::readyRead, reply, [&] {
        if (reply->bytesAvailable() < 188 * 10) return;
        if (firstByteMs && *firstByteMs < 0) *firstByteMs = clock.elapsed();
        code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done = true;
      });
      QObject::connect(reply, &QNetworkReply::finished, reply, [&] {
        code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done = true;
      });
      waitFor([&] { return done; }, timeoutMs);
      reply->abort();
      reply->deleteLater();
      return code;
    }
  };
} // namespace

TEST_CASE("The stand's Live TV is there for the test user") {
  if (!hasExtrasStand()) return;
  Stand s;
  LiveTvApi liveTv(*s.client);
  auto info = run<LiveTvInfo>([&](auto done) { liveTv.info(nullptr, done); });
  REQUIRE(info.ok());
  CHECK(info->isEnabled);
  CHECK(info->availableTo(s.client->userId()));
}

TEST_CASE("All channels with now and next on the screen's model, within a second") {
  if (!hasExtrasStand()) return;
  Stand s;
  library::LiveTvChannels model(*s.client);
  QElapsedTimer clock;
  clock.start();
  model.reload();
  REQUIRE(waitFor([&] { return !model.loading(); }, 30000));
  const qint64 elapsed = clock.elapsed();
  MESSAGE("channels and guide window loaded in " << elapsed << " ms");
  CHECK(model.errorText().isEmpty());
  REQUIRE(model.count() == expectedChannels());
  // Threshold "Live TV: data" (50 channels x 24 h < 1 s); generous in CI.
  CHECK(elapsed < 1000);
  for (int row = 0; row < model.count(); ++row) {
    INFO("row " << row);
    CHECK_FALSE(model.data(model.index(row), library::LiveTvChannels::NowTitleRole).toString().isEmpty());
    CHECK_FALSE(model.data(model.index(row), library::LiveTvChannels::NextTitleRole).toString().isEmpty());
  }
  // The guide maps channel n to "Channel n at HH:MM".
  const QString first = model.data(model.index(0), library::LiveTvChannels::NameRole).toString();
  CHECK(first == QStringLiteral("Test Channel 1"));
  CHECK(model.data(model.index(0), library::LiveTvChannels::NowTitleRole)
            .toString()
            .startsWith(QStringLiteral("Channel 1 at ")));
}

TEST_CASE("A channel plays through the server and its stream closes on Stopped") {
  if (!hasExtrasStand()) return;
  Stand s;
  LiveTvApi liveTv(*s.client);
  LiveTvChannelQuery query;
  query.limit = 1;
  auto channels = run<LiveTvChannelPage>([&](auto done) { liveTv.channels(query, nullptr, done); });
  REQUIRE(channels.ok());
  REQUIRE(channels->channels.size() == 1);
  const QString channelId = channels->channels.front().id;

  PlaybackInfoRequest request;
  request.itemId = channelId;
  request.deviceProfile = buildDeviceProfile({});
  auto info = run<PlaybackInfo>([&](auto done) { s.client->playbackInfo(request, nullptr, done); });
  REQUIRE(info.ok());
  REQUIRE(info->errorCode.isEmpty());
  REQUIRE_FALSE(info->mediaSources.isEmpty());
  const MediaSource& source = info->mediaSources.front();
  CHECK(source.isLive());
  REQUIRE_FALSE(source.liveStreamId.isEmpty());
  MESSAGE("live source: protocol " << source.protocol.toStdString() << ", direct stream "
                                   << source.supportsDirectStream << ", path " << source.path.toStdString());

  auto stream = resolvePlaybackStream(s.client->streamContext(), channelId, *info, source);
  REQUIRE(stream.ok());
  CHECK(stream->isLive);
  CHECK(stream->url.host() == s.client->serverUrl().host());
  CHECK(stream->url.port() == s.client->serverUrl().port());

  qint64 firstByteMs = -1;
  CHECK(s.status(stream->url, &firstByteMs) == 200);
  MESSAGE("first bytes of the channel after " << firstByteMs << " ms");
  CHECK(firstByteMs >= 0);
  CHECK(firstByteMs < 3000);

  // The opened stream's file on the server goes away once it is closed
  // (threshold: closed within 5 s of stopping). [The path is the server's
  // own LiveStreamFiles route, read from the source; seen on 12.2.]
  const QUrl own(source.path);
  if (!own.path().contains(QStringLiteral("/LiveTv/LiveStreamFiles/"))) {
    MESSAGE("the source has no LiveStreamFiles path; the close is not checked here");
    return;
  }
  QUrl file = s.client->serverUrl();
  QString prefix = file.path();
  while (prefix.endsWith(QLatin1Char('/'))) prefix.chop(1);
  file.setPath(prefix + own.path());
  // Open until Stopped names it.
  CHECK(s.status(file) == 200);

  PlaybackProgress progress;
  progress.itemId = channelId;
  progress.mediaSourceId = source.id;
  progress.playSessionId = info->playSessionId;
  progress.playMethod = stream->method;
  progress.liveStreamId = stream->liveStreamId;
  progress.canSeek = false;
  REQUIRE(run<Empty>([&](auto done) { s.client->reportPlaybackStart(progress, nullptr, done); }).ok());
  REQUIRE(run<Empty>([&](auto done) { s.client->reportPlaybackStopped(progress, false, nullptr, done); }).ok());

  QElapsedTimer clock;
  clock.start();
  int code = 0;
  while (clock.elapsed() < 5000) {
    code = s.status(file, nullptr, 2000);
    if (code == 404) break;
    waitFor([] { return false; }, 250);
  }
  MESSAGE("stream file answered " << code << " " << clock.elapsed() << " ms after Stopped");
  CHECK(code == 404);

  // Closing again is harmless.
  CHECK(run<Empty>([&](auto done) { liveTv.closeLiveStream(stream->liveStreamId, nullptr, done); }).ok());
}

TEST_CASE("The guide grid: every channel and the guide in view within a second, the rest by chunks") {
  if (!hasStand()) return;
  Stand s;
  library::LiveTvGuide guide(*s.client);
  guide.setViewMinutes(180);
  QElapsedTimer clock;
  clock.start();
  guide.reload();
  REQUIRE(waitFor([&] { return !guide.loading(); }, 30000));
  const qint64 elapsed = clock.elapsed();
  MESSAGE("guide grid (channels and the first chunk) loaded in " << elapsed << " ms");
  CHECK(guide.errorText().isEmpty());
  REQUIRE(guide.count() == expectedChannels());
  // Threshold "Live TV: data" (< 1 s on screen).
  CHECK(elapsed < 1000);
  for (int row = 0; row < guide.count(); ++row) {
    INFO("row " << row);
    CHECK_FALSE(guide.data(guide.index(row), library::LiveTvGuide::ProgramsRole).toList().isEmpty());
  }
  // The focus is on what is on now on the first channel.
  CHECK(guide.focusRow() == 0);
  CHECK(guide.focusedProgram().value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Channel 1 at ")));

  // Across the rows and along one: the guide's programmes follow each other.
  CHECK(guide.moveFocus(0, 1));
  CHECK(guide.focusedProgram().value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Channel 2 at ")));
  const QString before = guide.focusedProgram().value(QStringLiteral("id")).toString();
  CHECK(guide.moveFocus(1, 0));
  CHECK(guide.focusedProgram().value(QStringLiteral("id")).toString() != before);

  // Paging through the next hours loads the chunks the view reaches.
  clock.restart();
  for (int i = 0; i < 4; ++i) guide.pageFocus(1);
  REQUIRE(waitFor([&] { return !guide.loading(); }, 30000));
  MESSAGE("12 hours on: " << guide.chunksRequested() << " chunks, " << clock.elapsed() << " ms");
  CHECK(guide.chunksRequested() >= 2);
  CHECK(guide.viewStartMinutes() >= 600);
  CHECK_FALSE(guide.data(guide.index(1), library::LiveTvGuide::ProgramsRole).toList().isEmpty());
}

TEST_CASE("On now and a channel's page on the stand") {
  if (!hasStand()) return;
  Stand s;
  library::LiveTvOnNow onNow(*s.client);
  onNow.reload();
  REQUIRE(waitFor([&] { return onNow.count() > 0; }, 30000));
  CHECK(onNow.count() == std::min(24, expectedChannels()));
  CHECK(onNow.data(onNow.index(0), library::ItemListModel::NameRole).toString().startsWith(QStringLiteral("Channel 1 at ")));
  CHECK(onNow.data(onNow.index(0), library::ItemListModel::TypeRole).toString() == QStringLiteral("TvChannel"));
  const QString channelId = onNow.data(onNow.index(0), library::ItemListModel::IdRole).toString();

  LiveTvApi liveTv(*s.client);
  auto channel = run<LiveTvChannel>([&](auto done) { liveTv.channel(channelId, nullptr, done); });
  REQUIRE(channel.ok());
  CHECK(channel->name == QStringLiteral("Test Channel 1"));

  library::LiveTvSchedule schedule(*s.client);
  schedule.setChannelId(channelId);
  REQUIRE(waitFor([&] { return !schedule.loading(); }, 30000));
  CHECK(schedule.errorText().isEmpty());
  CHECK(schedule.name() == QStringLiteral("Test Channel 1"));
  REQUIRE(schedule.count() > 1);
  CHECK(schedule.data(schedule.index(0), library::LiveTvSchedule::OnNowRole).toBool());
  for (const LiveTvProgram& p : schedule.programs()) CHECK(p.channelId == channelId);
}
