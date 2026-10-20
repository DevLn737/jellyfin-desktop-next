#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/api/SyncPlayApi.h>
#include <jfd/library/ItemBrowser.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>

#include <memory>
#include <optional>

// Runs against a live server started by scripts/test-server. Configuration:
//   JFD_TEST_SERVER_URL, JFD_TEST_USER, JFD_TEST_PASSWORD
//   JFD_TEST_SERVER_FORK=1 when the server has the SyncPlay playback-rate
//   extension (12.2-syncplay.1); otherwise it is expected to be stock.

using namespace jfd;
using namespace jfd::api;
using jfd::test::waitFor;

namespace {
  QString env(const char* name) {
    return qEnvironmentVariable(name);
  }
  bool isFork() {
    return env("JFD_TEST_SERVER_FORK") == QLatin1String("1");
  }

  template <class T> Result<T> run(std::function<void(Callback<T>)> call, int timeoutMs = 20000) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    if (!waitFor([&] { return out.has_value(); }, timeoutMs)) {
      ApiError error;
      error.kind = ApiError::Kind::Timeout;
      error.message = QStringLiteral("test timed out");
      return error;
    }
    return std::move(*out);
  }

  std::unique_ptr<JellyfinClient> makeClient(const QString& deviceId) {
    auto client = std::make_unique<JellyfinClient>(
        ClientIdentity{QStringLiteral("Jellyfin Desktop Next Tests"), QStringLiteral("0.0.1"),
                       QStringLiteral("CI ") + deviceId, deviceId});
    const auto url = JellyfinClient::normalizeServerUrl(env("JFD_TEST_SERVER_URL"));
    REQUIRE_MESSAGE(url.has_value(),
                    "Set JFD_TEST_SERVER_URL (scripts/test-server/stock-server.sh up)");
    client->setServerUrl(*url);
    return client;
  }

  std::unique_ptr<JellyfinClient> signedIn(const QString& deviceId) {
    auto client = makeClient(deviceId);
    auto auth = run<AuthenticationResult>([&](auto done) {
      client->signIn(env("JFD_TEST_USER"), env("JFD_TEST_PASSWORD"), nullptr, done);
    });
    REQUIRE_MESSAGE(auth.ok(), (auth.ok() ? QString() : auth.error().message).toStdString());
    return client;
  }

  QByteArray fetch(const QUrl& url, int maxBytes = 4096) {
    // Media URLs must work without our headers, as mpv opens them.
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setRawHeader("Range", "bytes=0-" + QByteArray::number(maxBytes - 1));
    QNetworkReply* reply = network.get(request);
    waitFor([&] { return reply->isFinished(); }, 30000);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    reply->deleteLater();
    CHECK_MESSAGE((status == 200 || status == 206),
                  "HTTP " << status << " for " << url.toString().toStdString());
    return data;
  }

  Item findItem(JellyfinClient& client, const QString& type) {
    ItemQuery query;
    query.includeItemTypes = {type};
    auto page = run<ItemPage>([&](auto done) { client.items(query, nullptr, done); });
    REQUIRE(page.ok());
    REQUIRE_FALSE(page->items.isEmpty());
    return page->items.front();
  }

  SessionCapabilities capabilities(bool rate) {
    SessionCapabilities c;
    c.supportsSyncPlayPlaybackRate = rate;
    c.supportedCommands = {QStringLiteral("DisplayMessage"), QStringLiteral("SetAudioStreamIndex"),
                           QStringLiteral("SetSubtitleStreamIndex")};
    c.deviceProfile = buildDeviceProfile({});
    return c;
  }

  struct Member {
    std::unique_ptr<JellyfinClient> client;
    std::unique_ptr<SessionSocket> socket;
    std::unique_ptr<SyncPlayApi> syncPlay;
    QList<QJsonObject> updates;

    Member(const QString& deviceId, bool rate) : client(signedIn(deviceId)) {
      auto reported = run<Empty>(
          [&](auto done) { client->reportCapabilities(capabilities(rate), nullptr, done); });
      REQUIRE(reported.ok());
      socket = std::make_unique<SessionSocket>(*client);
      syncPlay = std::make_unique<SyncPlayApi>(*client);
      QObject::connect(socket.get(), &SessionSocket::syncPlayGroupUpdate, socket.get(),
                       [this](const QJsonObject& u) { updates.append(u); });
      QSignalSpy ready(socket.get(), &SessionSocket::ready);
      socket->start();
      REQUIRE(waitFor([&] { return ready.count() == 1; }, 20000));
    }

    bool sawUpdate(const char* type) const {
      for (const auto& u : updates)
        if (u.value(QStringLiteral("Type")).toString() == QLatin1String(type)) return true;
      return false;
    }
  };
} // namespace

TEST_CASE("Server info and version") {
  auto client = makeClient(QStringLiteral("jfd-info"));
  auto info = run<PublicServerInfo>([&](auto done) { client->publicServerInfo(nullptr, done); });
  REQUIRE(info.ok());
  CHECK_FALSE(info->id.isEmpty());
  CHECK(info->startupWizardCompleted);
  CHECK(info->version >= (ServerVersion{12, 2, 0}));
  MESSAGE("Server " << info->versionText.toStdString());
}

TEST_CASE("Password sign-in, rejected password and sign-out") {
  auto client = makeClient(QStringLiteral("jfd-signin"));
  auto wrong = run<AuthenticationResult>([&](auto done) {
    client->signIn(env("JFD_TEST_USER"), QStringLiteral("wrong"), nullptr, done);
  });
  REQUIRE_FALSE(wrong.ok());
  CHECK(wrong.error().isUnauthorized());
  CHECK_FALSE(client->isSignedIn());

  auto auth = run<AuthenticationResult>([&](auto done) {
    client->signIn(env("JFD_TEST_USER"), env("JFD_TEST_PASSWORD"), nullptr, done);
  });
  REQUIRE(auth.ok());
  CHECK(auth->user.name == env("JFD_TEST_USER"));
  auto me = run<UserInfo>([&](auto done) { client->currentUser(nullptr, done); });
  REQUIRE(me.ok());
  CHECK(me->id == auth->user.id);

  const QString token = client->accessToken();
  auto out = run<Empty>([&](auto done) { client->signOut(nullptr, done); });
  CHECK(out.ok());
  CHECK_FALSE(client->isSignedIn());
  // The revoked token no longer works.
  client->setSession(token, auth->user.id);
  auto after = run<UserInfo>([&](auto done) { client->currentUser(nullptr, done); });
  REQUIRE_FALSE(after.ok());
  CHECK(after.error().isUnauthorized());
}

TEST_CASE("Quick Connect sign-in") {
  auto admin = signedIn(QStringLiteral("jfd-qc-admin"));
  auto client = makeClient(QStringLiteral("jfd-qc-device"));
  auto enabled = run<bool>([&](auto done) { client->quickConnectEnabled(nullptr, done); });
  REQUIRE(enabled.ok());
  REQUIRE(enabled.value());

  auto started =
      run<QuickConnectState>([&](auto done) { client->quickConnectInitiate(nullptr, done); });
  REQUIRE(started.ok());
  CHECK_FALSE(started->code.isEmpty());
  CHECK_FALSE(started->authenticated);

  // The user approves the code from a signed-in device.
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("code"), started->code);
  auto approved = run<JellyfinClient::RawResponse>([&](auto done) {
    admin->send(JellyfinClient::Method::Post, QStringLiteral("QuickConnect/Authorize"), query,
                std::nullopt, nullptr, done);
  });
  REQUIRE(approved.ok());

  auto state = run<QuickConnectState>(
      [&](auto done) { client->quickConnectState(started->secret, nullptr, done); });
  REQUIRE(state.ok());
  CHECK(state->authenticated);
  auto auth = run<AuthenticationResult>(
      [&](auto done) { client->quickConnectSignIn(started->secret, nullptr, done); });
  REQUIRE(auth.ok());
  CHECK(client->isSignedIn());
  CHECK(client->userId() == admin->userId());
}

TEST_CASE("Capabilities register the session") {
  auto client = signedIn(QStringLiteral("jfd-caps"));
  auto reported =
      run<Empty>([&](auto done) { client->reportCapabilities(capabilities(true), nullptr, done); });
  REQUIRE(reported.ok());
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("deviceId"), QStringLiteral("jfd-caps"));
  auto sessions = run<JellyfinClient::RawResponse>([&](auto done) {
    client->send(JellyfinClient::Method::Get, QStringLiteral("Sessions"), query, std::nullopt,
                 nullptr, done);
  });
  REQUIRE(sessions.ok());
  const QJsonArray list = QJsonDocument::fromJson(sessions->body).array();
  REQUIRE(list.size() == 1);
  const QJsonObject caps = list.at(0).toObject().value(QStringLiteral("Capabilities")).toObject();
  CHECK(caps.value(QStringLiteral("SupportsMediaControl")).toBool());
  CHECK(caps.value(QStringLiteral("PlayableMediaTypes")).toArray().size() == 2);
  if (isFork()) CHECK(caps.value(QStringLiteral("SupportsSyncPlayPlaybackRate")).toBool());
}

TEST_CASE("Search, series, seasons and episodes through the library model") {
  auto client = signedIn(QStringLiteral("jfd-library"));
  library::ItemBrowser browser(*client);
  QSignalSpy play(&browser, &library::ItemBrowser::playRequested);
  browser.search(QStringLiteral("Test"));
  REQUIRE(waitFor([&] { return !browser.loading(); }, 20000));
  REQUIRE(browser.errorText().isEmpty());
  int seriesRow = -1;
  bool movie = false;
  for (int row = 0; row < browser.model()->count(); ++row) {
    const auto& item = browser.model()->entry(row)->item;
    if (item.type == QLatin1String("Series")) seriesRow = row;
    if (item.type == QLatin1String("Movie")) movie = true;
  }
  CHECK(movie);
  REQUIRE(seriesRow >= 0);

  browser.activate(seriesRow);
  REQUIRE(waitFor(
      [&] {
        return !browser.loading() && browser.model()->count() > 0 &&
               browser.model()->entry(0)->item.type == QLatin1String("Season");
      },
      20000));
  browser.activate(0);
  REQUIRE(waitFor(
      [&] {
        return !browser.loading() && browser.model()->count() == 2 &&
               browser.model()->entry(0)->item.type == QLatin1String("Episode");
      },
      20000));
  CHECK(browser.model()->entry(0)->item.indexNumber == 1);
  CHECK(browser.model()->entry(1)->item.indexNumber == 2);
  browser.activate(1);
  REQUIRE(play.count() == 1);
}

TEST_CASE("Home rows, library listings, filters and similar items") {
  auto client = signedIn(QStringLiteral("jfd-home"));
  auto views = run<ItemPage>([&](auto done) { client->views(nullptr, done); });
  REQUIRE(views.ok());
  QString shows;
  QString movies;
  for (const Item& view : views->items) {
    if (view.collectionType == QLatin1String("tvshows")) shows = view.id;
    if (view.collectionType == QLatin1String("movies")) movies = view.id;
  }
  REQUIRE_FALSE(shows.isEmpty());
  REQUIRE_FALSE(movies.isEmpty());

  auto latest = run<ItemPage>([&](auto done) { client->latestItems(shows, 16, nullptr, done); });
  REQUIRE(latest.ok());
  REQUIRE(latest->items.size() == 1);
  // Episodes are grouped into their series.
  CHECK(latest->items[0].type == QStringLiteral("Series"));
  CHECK(latest->items[0].userData.unplayedItemCount == 2);

  CHECK(run<ItemPage>([&](auto done) { client->resumeItems(12, {}, nullptr, done); }).ok());
  CHECK(run<ItemPage>([&](auto done) { client->nextUp(12, {}, nullptr, done); }).ok());

  ItemQuery query;
  query.parentId = movies;
  query.includeItemTypes = {QStringLiteral("Movie")};
  query.sortBy = QStringLiteral("SortName");
  query.isPlayed = false;
  query.limit = 1;
  auto page = run<ItemPage>([&](auto done) { client->items(query, nullptr, done); });
  REQUIRE(page.ok());
  CHECK(page->totalRecordCount >= 1);
  CHECK(page->items.size() == 1);
  query.years = {1900};
  page = run<ItemPage>([&](auto done) { client->items(query, nullptr, done); });
  REQUIRE(page.ok());
  CHECK(page->totalRecordCount == 0);

  auto filters = run<ItemFilters>(
      [&](auto done) { client->itemFilters(movies, {QStringLiteral("Movie")}, nullptr, done); });
  CHECK(filters.ok());
  CHECK(run<ItemPage>([&](auto done) {
          client->similarItems(latest->items[0].id, 6, nullptr, done);
        }).ok());
}

TEST_CASE("Favorites, details with trickplay, tiles and media segments") {
  auto client = signedIn(QStringLiteral("jfd-details"));
  const Item episode = findItem(*client, QStringLiteral("Episode"));

  auto on =
      run<UserItemData>([&](auto done) { client->setFavorite(episode.id, true, nullptr, done); });
  REQUIRE(on.ok());
  CHECK(on->isFavorite);
  auto details = run<Item>([&](auto done) { client->itemDetails(episode.id, nullptr, done); });
  REQUIRE(details.ok());
  CHECK(details->userData.isFavorite);
  auto off =
      run<UserItemData>([&](auto done) { client->setFavorite(episode.id, false, nullptr, done); });
  REQUIRE(off.ok());
  CHECK_FALSE(off->isFavorite);

  // scripts/test-server generates trickplay tiles for the episodes.
  REQUIRE(details->trickplay.size() == 1);
  const QString source = details->trickplay.keys().front();
  const TrickplayInfo info = details->trickplay.value(source).front();
  CHECK(info.intervalMs > 0);
  CHECK(info.thumbnailCount >= 30);
  const auto frame = trickplayFrame(info, 200'000);
  REQUIRE(frame);
  const QUrl tile = client->trickplayTileUrl(episode.id, source, info.width, frame->tileIndex);
  auto jpeg = run<JellyfinClient::RawResponse>(
      [&](auto done) { client->authorizedGet(tile, nullptr, done); });
  REQUIRE(jpeg.ok());
  CHECK(jpeg->body.startsWith("\xFF\xD8"));

  ImageRequest image;
  image.itemId = episode.id;
  image.tag = episode.primaryImageTag;
  image.fillWidth = 200;
  const QByteArray poster = fetch(client->imageUrl(image), 1 << 20);
  CHECK(poster.startsWith("\xFF\xD8"));

  // A stock server has no segment provider: an empty list, not an error.
  auto segments = run<QList<MediaSegment>>(
      [&](auto done) { client->mediaSegments(episode.id, nullptr, done); });
  REQUIRE(segments.ok());
  CHECK(segments->isEmpty());
}

TEST_CASE("PlaybackInfo resolves to streams mpv can open") {
  auto client = signedIn(QStringLiteral("jfd-playback"));
  const Item movie = findItem(*client, QStringLiteral("Movie"));

  PlaybackInfoRequest request;
  request.itemId = movie.id;
  request.deviceProfile = buildDeviceProfile({});
  auto info = run<PlaybackInfo>([&](auto done) { client->playbackInfo(request, nullptr, done); });
  REQUIRE(info.ok());
  REQUIRE(info->mediaSources.size() == 1);
  const MediaSource& source = info->mediaSources.front();

  auto stream = resolvePlaybackStream(client->streamContext(), movie.id, info.value(), source);
  REQUIRE(stream.ok());
  CHECK(stream->method == PlayMethod::DirectPlay);
  // Matroska starts with the EBML magic number.
  CHECK(fetch(stream->url).startsWith(QByteArray::fromHex("1a45dfa3")));

  REQUIRE(stream->externalSubtitles.size() == 1);
  CHECK(fetch(stream->externalSubtitles.front().url).contains("Hello"));

  // Two embedded audio tracks map to mpv ids 1 and 2 whatever their Index.
  QList<int> audioIds;
  for (const auto& s : source.mediaStreams)
    if (s.type == QLatin1String("Audio") && !s.isExternal)
      audioIds.append(*embeddedTrackId(source, s.index));
  CHECK(audioIds == QList<int>{1, 2});

  request.enableDirectPlay = false;
  request.enableDirectStream = false;
  auto transcodeInfo =
      run<PlaybackInfo>([&](auto done) { client->playbackInfo(request, nullptr, done); });
  REQUIRE(transcodeInfo.ok());
  auto transcode = resolvePlaybackStream(client->streamContext(), movie.id, transcodeInfo.value(),
                                         transcodeInfo->mediaSources.front());
  REQUIRE(transcode.ok());
  CHECK(transcode->method == PlayMethod::Transcode);
  CHECK(transcode->isHls);
  CHECK(fetch(transcode->url).startsWith("#EXTM3U"));
}

TEST_CASE("Playback reports update resume position and watched state") {
  auto client = signedIn(QStringLiteral("jfd-reports"));
  const Item episode = findItem(*client, QStringLiteral("Episode"));
  PlaybackInfoRequest request;
  request.itemId = episode.id;
  request.deviceProfile = buildDeviceProfile({});
  auto info = run<PlaybackInfo>([&](auto done) { client->playbackInfo(request, nullptr, done); });
  REQUIRE(info.ok());

  PlaybackProgress progress;
  progress.itemId = episode.id;
  progress.mediaSourceId = info->mediaSources.front().id;
  progress.playSessionId = info->playSessionId;
  progress.playMethod = PlayMethod::DirectPlay;
  REQUIRE(
      run<Empty>([&](auto done) { client->reportPlaybackStart(progress, nullptr, done); }).ok());
  progress.positionTicks = 1'200'000'000;
  progress.isPaused = true;
  progress.eventName = QStringLiteral("Pause");
  REQUIRE(
      run<Empty>([&](auto done) { client->reportPlaybackProgress(progress, nullptr, done); }).ok());
  REQUIRE(run<Empty>([&](auto done) {
            client->reportPlaybackStopped(progress, false, nullptr, done);
          }).ok());

  auto after = run<Item>([&](auto done) { client->item(episode.id, nullptr, done); });
  REQUIRE(after.ok());
  CHECK(after->userData.playbackPositionTicks == 1'200'000'000);

  REQUIRE(run<Empty>([&](auto done) { client->markPlayed(episode.id, nullptr, done); }).ok());
  auto played = run<Item>([&](auto done) { client->item(episode.id, nullptr, done); });
  REQUIRE(played.ok());
  CHECK(played->userData.played);
  REQUIRE(run<Empty>([&](auto done) { client->markUnplayed(episode.id, nullptr, done); }).ok());
  auto unplayed = run<Item>([&](auto done) { client->item(episode.id, nullptr, done); });
  REQUIRE(unplayed.ok());
  CHECK_FALSE(unplayed->userData.played);
}

TEST_CASE("SyncPlay: create, join, queue, ready, clock and leave") {
  Member host(QStringLiteral("jfd-sp-host"), true);
  Member guest(QStringLiteral("jfd-sp-guest"), true);

  auto group = run<SyncPlayGroup>([&](auto done) {
    host.syncPlay->createGroup(QStringLiteral("jfd test group"), nullptr, done);
  });
  REQUIRE(group.ok());
  REQUIRE(waitFor([&] { return host.sawUpdate("GroupJoined"); }, 10000));

  REQUIRE(run<Empty>([&](auto done) {
            guest.syncPlay->joinGroup(group->groupId, nullptr, done);
          }).ok());
  REQUIRE(waitFor([&] { return guest.sawUpdate("GroupJoined"); }, 10000));
  REQUIRE(waitFor([&] { return host.sawUpdate("UserJoined"); }, 10000));

  auto listed =
      run<SyncPlayGroup>([&](auto done) { host.syncPlay->group(group->groupId, nullptr, done); });
  REQUIRE(listed.ok());
  // Participants are user names; both members are the same user here.
  CHECK(listed->participants.contains(env("JFD_TEST_USER")));

  const Item movie = findItem(*host.client, QStringLiteral("Movie"));
  REQUIRE(run<Empty>([&](auto done) {
            host.syncPlay->setNewQueue({movie.id}, 0, 0, nullptr, done);
          }).ok());
  REQUIRE(waitFor([&] { return guest.sawUpdate("PlayQueue"); }, 10000));
  QString playlistItemId;
  for (const auto& u : guest.updates)
    if (u.value(QStringLiteral("Type")).toString() == QLatin1String("PlayQueue"))
      playlistItemId = u.value(QStringLiteral("Data"))
                           .toObject()
                           .value(QStringLiteral("Playlist"))
                           .toArray()
                           .at(0)
                           .toObject()
                           .value(QStringLiteral("PlaylistItemId"))
                           .toString();
  REQUIRE_FALSE(playlistItemId.isEmpty());

  SyncPlayReadyState state;
  state.when = systemClockNow();
  state.playlistItemId = playlistItemId;
  CHECK(run<Empty>([&](auto done) { host.syncPlay->buffering(state, nullptr, done); }).ok());
  CHECK(run<Empty>([&](auto done) { host.syncPlay->ready(state, nullptr, done); }).ok());
  CHECK(run<Empty>([&](auto done) { guest.syncPlay->ready(state, nullptr, done); }).ok());
  CHECK(run<Empty>([&](auto done) { host.syncPlay->ping(20, nullptr, done); }).ok());

  class SystemClock final : public core::Clock {
  public:
    core::LocalInstant now() const override {
      return core::LocalInstant::fromSinceOrigin(systemClockNow().sinceOrigin());
    }
  } clock;
  auto sample =
      run<UtcTimeSample>([&](auto done) { host.syncPlay->utcTime(clock, nullptr, done); });
  REQUIRE(sample.ok());
  // Server and test share a machine: the offset is near zero.
  const auto offset =
      (sample->requestReception.sinceOrigin() - sample->requestSent.sinceOrigin()).count();
  CHECK(std::abs(offset) < 5 * 10'000'000LL);
  CHECK(sample->responseTransmission >= sample->requestReception);

  CHECK(run<Empty>([&](auto done) { guest.syncPlay->leaveGroup(nullptr, done); }).ok());
  CHECK(run<Empty>([&](auto done) { host.syncPlay->leaveGroup(nullptr, done); }).ok());
}

TEST_CASE("SyncPlay playback rate: 409 for unsupported members, Unsupported on a stock server") {
  Member host(QStringLiteral("jfd-rate-host"), true);
  Member legacy(QStringLiteral("jfd-rate-legacy"), false);

  auto probe = run<bool>([&](auto done) { host.client->probeSyncPlayPlaybackRate(nullptr, done); });
  REQUIRE(probe.ok());
  CHECK(probe.value() == isFork());

  auto group = run<SyncPlayGroup>([&](auto done) {
    host.syncPlay->createGroup(QStringLiteral("jfd rate group"), nullptr, done);
  });
  REQUIRE(group.ok());
  REQUIRE(run<Empty>([&](auto done) {
            legacy.syncPlay->joinGroup(group->groupId, nullptr, done);
          }).ok());
  REQUIRE(waitFor([&] { return legacy.sawUpdate("GroupJoined"); }, 10000));

  auto faster = run<Empty>([&](auto done) { host.syncPlay->setPlaybackRate(1.5, nullptr, done); });
  REQUIRE_FALSE(faster.ok());
  if (!isFork()) {
    // A stock server has no route: the group stays at 1x.
    CHECK(faster.error().kind == ApiError::Kind::Unsupported);
    // Inside a group the probe must not change anything either.
    auto again =
        run<bool>([&](auto done) { host.client->probeSyncPlayPlaybackRate(nullptr, done); });
    REQUIRE(again.ok());
    CHECK_FALSE(again.value());
  } else {
    CHECK(faster.error().isPlaybackRateConflict());
    CHECK(faster.error().unsupportedParticipants() == 1);
    // 1x is always allowed in a mixed group.
    CHECK(run<Empty>([&](auto done) { host.syncPlay->setPlaybackRate(1.0, nullptr, done); }).ok());
    // The probe inside a group sees 400 and changes nothing.
    auto again =
        run<bool>([&](auto done) { host.client->probeSyncPlayPlaybackRate(nullptr, done); });
    REQUIRE(again.ok());
    CHECK(again.value());

    // Once only supporting members remain, the rate applies; a member without
    // support then cannot join the accelerated group.
    REQUIRE(run<Empty>([&](auto done) { legacy.syncPlay->leaveGroup(nullptr, done); }).ok());
    REQUIRE(
        run<Empty>([&](auto done) { host.syncPlay->setPlaybackRate(1.5, nullptr, done); }).ok());
    auto join =
        run<Empty>([&](auto done) { legacy.syncPlay->joinGroup(group->groupId, nullptr, done); });
    REQUIRE_FALSE(join.ok());
    CHECK(join.error().isPlaybackRateConflict());
  }
  run<Empty>([&](auto done) { legacy.syncPlay->leaveGroup(nullptr, done); });
  CHECK(run<Empty>([&](auto done) { host.syncPlay->leaveGroup(nullptr, done); }).ok());
}
