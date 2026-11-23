#include "../support/DoctestQt.h"

#include "../support/FakeServer.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SyncPlayApi.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <algorithm>
#include <memory>
#include <optional>

using namespace jfd;
using namespace jfd::api;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;

namespace {
  ClientIdentity identity() {
    return ClientIdentity{QStringLiteral("Test Client"), QStringLiteral("1.0"),
                          QStringLiteral("Test Device"), QStringLiteral("device1")};
  }

  const char* AuthJson = R"({
    "AccessToken": "token1", "ServerId": "server1",
    "SessionInfo": {"Id": "session1"},
    "User": {"Id": "user1", "Name": "alice", "ServerId": "server1",
             "Policy": {"SyncPlayAccess": "CreateAndJoinGroups"}}
  })";

  template <class T> std::optional<Result<T>> run(std::function<void(Callback<T>)> call) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    REQUIRE(waitFor([&] { return out.has_value(); }));
    return out;
  }

  class FixedClock final : public core::Clock {
  public:
    core::LocalInstant now() const override {
      m_ticks += 1000;
      return core::LocalInstant::fromSinceOrigin(core::WallTicks(m_ticks));
    }

  private:
    mutable std::int64_t m_ticks = 0;
  };
} // namespace

TEST_CASE("Server URLs are normalized") {
  CHECK(JellyfinClient::normalizeServerUrl(QStringLiteral("media.lan:8096"))->toString() ==
        QStringLiteral("http://media.lan:8096"));
  CHECK(JellyfinClient::normalizeServerUrl(QStringLiteral(" https://x.org/jellyfin/web/ "))
            ->toString() == QStringLiteral("https://x.org/jellyfin"));
  CHECK_FALSE(JellyfinClient::normalizeServerUrl(QStringLiteral("ftp://x.org")));
  CHECK_FALSE(JellyfinClient::normalizeServerUrl(QString()));

  JellyfinClient client(identity());
  client.setServerUrl(QUrl(QStringLiteral("https://x.org/jellyfin")));
  CHECK(client.url(QStringLiteral("/Items/abc")).toString() ==
        QStringLiteral("https://x.org/jellyfin/Items/abc"));
}

TEST_CASE("Sign-in sends the identity, stores the token and uses it afterwards") {
  FakeServer server([](const HttpRequest& r) -> HttpResponse {
    if (r.path == QLatin1String("/Users/AuthenticateByName")) return jfd::test::json(AuthJson);
    if (r.path == QLatin1String("/Users/Me"))
      return jfd::test::json(R"({"Id": "user1", "Name": "alice"})");
    return {404, {}, {}, 0};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  QSignalSpy changed(&client, &JellyfinClient::sessionChanged);

  auto auth = run<AuthenticationResult>([&](auto done) {
    client.signIn(QStringLiteral("alice"), QStringLiteral("pw"), nullptr, done);
  });
  REQUIRE(*auth);
  CHECK((*auth)->accessToken == QStringLiteral("token1"));
  CHECK((*auth)->user.syncPlayAccess == QStringLiteral("CreateAndJoinGroups"));
  CHECK(client.accessToken() == QStringLiteral("token1"));
  CHECK(client.userId() == QStringLiteral("user1"));
  CHECK(changed.count() == 1);

  const HttpRequest& signInRequest = server.requests().at(0);
  CHECK(signInRequest.method == "POST");
  CHECK_FALSE(signInRequest.headers.value("authorization").contains("Token="));
  CHECK(signInRequest.headers.value("authorization").contains("DeviceId=\"device1\""));
  const auto body = QJsonDocument::fromJson(signInRequest.body).object();
  CHECK(body.value(QStringLiteral("Username")).toString() == QStringLiteral("alice"));
  CHECK(body.value(QStringLiteral("Pw")).toString() == QStringLiteral("pw"));

  auto me = run<UserInfo>([&](auto done) { client.currentUser(nullptr, done); });
  REQUIRE(*me);
  CHECK(server.requests().at(1).headers.value("authorization").contains("Token=\"token1\""));
}

TEST_CASE("A rejected token clears the session and reports it once") {
  FakeServer server([](const HttpRequest&) { return HttpResponse{401, {}, {}, 0}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("old"), QStringLiteral("user1"));
  QSignalSpy expired(&client, &JellyfinClient::sessionExpired);

  auto result = run<UserInfo>([&](auto done) { client.currentUser(nullptr, done); });
  REQUIRE_FALSE(*result);
  CHECK(result->error().isUnauthorized());
  // Expired only after GET Users/Me confirmed the token is refused.
  REQUIRE(waitFor([&] { return expired.count() == 1; }));
  CHECK_FALSE(client.isSignedIn());
  REQUIRE(server.requests().size() == 2);
  CHECK(server.requests().at(1).path == QStringLiteral("/Users/Me"));
  CHECK(server.requests().at(1).headers.value("authorization").contains("Token=\"old\""));
  waitFor([] { return false; }, 200);
  CHECK(expired.count() == 1);
}

TEST_CASE("A 401 the token check does not confirm keeps the session") {
  // A reverse proxy with its own sign-in refuses one path; Jellyfin accepts
  // the token.
  FakeServer server([](const HttpRequest& r) {
    if (r.path == QLatin1String("/Users/Me")) return jfd::test::json(R"({"Id": "user1", "Name": "alice"})");
    return HttpResponse{401, {}, {}, 0};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
  QSignalSpy expired(&client, &JellyfinClient::sessionExpired);

  // Several refused requests at once: one check.
  int answered = 0;
  for (int i = 0; i < 3; ++i)
    client.rawRequest(JellyfinClient::Method::Get, QStringLiteral("Items"), {}, nullptr,
                      [&](int status, QByteArray) {
                        CHECK(status == 401);
                        ++answered;
                      });
  REQUIRE(waitFor([&] { return answered == 3; }));
  REQUIRE(waitFor([&] {
    return std::count_if(server.requests().begin(), server.requests().end(),
                         [](const HttpRequest& r) { return r.path == QLatin1String("/Users/Me"); }) == 1;
  }));
  waitFor([] { return false; }, 200);
  CHECK(expired.count() == 0);
  CHECK(client.accessToken() == QStringLiteral("token1"));

  // Right after a passed check, more 401s do not check again.
  bool againAnswered = false;
  client.rawRequest(JellyfinClient::Method::Get, QStringLiteral("Items"), {}, nullptr,
                    [&](int, QByteArray) { againAnswered = true; });
  REQUIRE(waitFor([&] { return againAnswered; }));
  waitFor([] { return false; }, 200);
  CHECK(std::count_if(server.requests().begin(), server.requests().end(),
                      [](const HttpRequest& r) { return r.path == QLatin1String("/Users/Me"); }) == 1);
  CHECK(client.isSignedIn());
}

TEST_CASE("A server that cannot be reached for the token check keeps the session") {
  FakeServer server([](const HttpRequest& r) {
    if (r.path == QLatin1String("/Users/Me")) return HttpResponse{503, {}, {}, 0};
    return HttpResponse{401, {}, {}, 0};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
  QSignalSpy expired(&client, &JellyfinClient::sessionExpired);
  bool answered = false;
  client.rawRequest(JellyfinClient::Method::Get, QStringLiteral("Items"), {}, nullptr,
                    [&](int, QByteArray) { answered = true; });
  REQUIRE(waitFor([&] { return answered && server.requests().size() == 2; }));
  waitFor([] { return false; }, 200);
  CHECK(expired.count() == 0);
  CHECK(client.isSignedIn());
}

TEST_CASE("A 401 for a token that is no longer current is ignored") {
  FakeServer server([](const HttpRequest& r) {
    if (r.path == QLatin1String("/Users/Me")) return HttpResponse{401, {}, {}, 0};
    return HttpResponse{401, {}, {}, 300};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("old"), QStringLiteral("user1"));
  QSignalSpy expired(&client, &JellyfinClient::sessionExpired);
  bool answered = false;
  client.rawRequest(JellyfinClient::Method::Get, QStringLiteral("Items"), {}, nullptr,
                    [&](int, QByteArray) { answered = true; });
  REQUIRE(waitFor([&] { return server.requests().size() == 1; }));
  // A new sign-in while the old token's request is under way.
  client.setSession(QStringLiteral("new"), QStringLiteral("user2"));
  REQUIRE(waitFor([&] { return answered; }));
  waitFor([] { return false; }, 200);
  CHECK(server.requests().size() == 1);
  CHECK(expired.count() == 0);
  CHECK(client.accessToken() == QStringLiteral("new"));
}

TEST_CASE("Signing out clears the session at once; a late Logout answer keeps a newer one") {
  FakeServer server([](const HttpRequest& r) {
    // A slow server: the answer comes after the user signed in again.
    if (r.path == QLatin1String("/Sessions/Logout")) return HttpResponse{401, {}, {}, 400};
    return HttpResponse{204, {}, {}, 0};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("old"), QStringLiteral("user1"));
  QSignalSpy expired(&client, &JellyfinClient::sessionExpired);
  std::optional<Result<Empty>> logout;
  client.signOut(nullptr, [&](Result<Empty> result) { logout.emplace(std::move(result)); });
  CHECK_FALSE(client.isSignedIn());
  REQUIRE(waitFor([&] { return server.requests().size() == 1; }));
  CHECK(server.requests().at(0).headers.value("authorization").contains("Token=\"old\""));

  client.setSession(QStringLiteral("new"), QStringLiteral("user2"));
  REQUIRE(waitFor([&] { return logout.has_value(); }));
  waitFor([] { return false; }, 200);
  CHECK(client.accessToken() == QStringLiteral("new"));
  CHECK(client.userId() == QStringLiteral("user2"));
  CHECK(expired.count() == 0);
  CHECK(server.requests().size() == 1);
}

TEST_CASE("revokeToken sends Logout with the given token and leaves the session alone") {
  FakeServer server([](const HttpRequest&) { return HttpResponse{204, {}, {}, 0}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("current"), QStringLiteral("user2"));
  client.revokeToken(QStringLiteral("old"));
  REQUIRE(waitFor([&] { return server.requests().size() == 1; }));
  CHECK(server.requests().at(0).method == "POST");
  CHECK(server.requests().at(0).path == QStringLiteral("/Sessions/Logout"));
  CHECK(server.requests().at(0).headers.value("authorization").contains("Token=\"old\""));
  CHECK(client.accessToken() == QStringLiteral("current"));
}

TEST_CASE("Old servers are refused") {
  FakeServer server([](const HttpRequest&) {
    return jfd::test::json(R"({"Id": "s", "ServerName": "old", "Version": "10.9.11"})");
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  auto info = run<PublicServerInfo>([&](auto done) { client.publicServerInfo(nullptr, done); });
  REQUIRE_FALSE(*info);
  CHECK(info->error().kind == ApiError::Kind::Unsupported);
  CHECK(info->error().problemCode == QStringLiteral("ServerTooOld"));
}

TEST_CASE("A callback whose context is gone is never called") {
  FakeServer server([](const HttpRequest&) {
    return HttpResponse{200, R"({"Id": "user1"})", "application/json", 200};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  bool called = false;
  auto screen = std::make_unique<QObject>();
  client.currentUser(screen.get(), [&](Result<UserInfo>) { called = true; });
  screen.reset();
  waitFor([&] { return server.requests().size() == 1; });
  waitFor([] { return false; }, 400);
  CHECK_FALSE(called);
}

TEST_CASE("A read whose context is gone is stopped and frees its connection") {
  FakeServer server([](const HttpRequest& r) {
    const int delay = r.path == QLatin1String("/Users/slow") ? 3000 : 0;
    return HttpResponse{200, R"({"Id": "user1"})", "application/json", delay};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
  // Six slow reads take every connection to the host.
  auto page = std::make_unique<QObject>();
  for (int i = 0; i < 6; ++i)
    client.rawRequest(JellyfinClient::Method::Get, QStringLiteral("Users/slow"), {}, page.get(),
                      [](int, QByteArray) {});
  REQUIRE(waitFor([&] { return server.requests().size() == 6; }));
  // The page is left: its reads stop, and the next request does not wait
  // for them.
  page.reset();
  QElapsedTimer timer;
  timer.start();
  auto result = run<UserInfo>([&](auto done) { client.currentUser(nullptr, done); });
  CHECK(*result);
  CHECK(timer.elapsed() < 2000);
}

TEST_CASE("A new sign-in clears the old session and says so") {
  FakeServer server([](const HttpRequest&) { return HttpResponse{401, {}, {}, 0}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("old"), QStringLiteral("user1"));
  QSignalSpy changed(&client, &JellyfinClient::sessionChanged);
  auto result = run<AuthenticationResult>(
      [&](auto done) { client.signIn(QStringLiteral("bob"), QStringLiteral("x"), nullptr, done); });
  REQUIRE_FALSE(*result);
  CHECK_FALSE(client.isSignedIn());
  CHECK(client.userId().isEmpty());
  CHECK(changed.count() >= 1);

  // Quick Connect too.
  client.setSession(QStringLiteral("old"), QStringLiteral("user1"));
  changed.clear();
  auto quick = run<AuthenticationResult>(
      [&](auto done) { client.quickConnectSignIn(QStringLiteral("secret"), nullptr, done); });
  REQUIRE_FALSE(*quick);
  CHECK(client.userId().isEmpty());
  CHECK(changed.count() >= 1);
}

TEST_CASE("Unreachable servers and timeouts are told apart") {
  FakeServer server(
      [](const HttpRequest&) { return HttpResponse{200, "{}", "application/json", 2000}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setTimeoutMs(300);
  auto slow = run<UserInfo>([&](auto done) { client.currentUser(nullptr, done); });
  REQUIRE_FALSE(*slow);
  CHECK(slow->error().kind == ApiError::Kind::Timeout);

  // Windows retries a refused localhost connection for about two seconds, so
  // the short timeout above would win there.
  client.setTimeoutMs(30'000);
  client.setServerUrl(QUrl(QStringLiteral("http://127.0.0.1:1")));
  auto refused = run<UserInfo>([&](auto done) { client.currentUser(nullptr, done); });
  REQUIRE_FALSE(*refused);
  CHECK(refused->error().kind == ApiError::Kind::Network);
}

TEST_CASE("Capabilities are reported in full and remembered") {
  FakeServer server([](const HttpRequest&) { return HttpResponse{204, {}, {}, 0}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
  SessionCapabilities capabilities;
  capabilities.supportsSyncPlayPlaybackRate = true;
  capabilities.supportedCommands = {QStringLiteral("DisplayMessage")};
  auto result =
      run<Empty>([&](auto done) { client.reportCapabilities(capabilities, nullptr, done); });
  REQUIRE(*result);
  const HttpRequest& request = server.requests().at(0);
  CHECK(request.path == QStringLiteral("/Sessions/Capabilities/Full"));
  const auto body = QJsonDocument::fromJson(request.body).object();
  CHECK(body.value(QStringLiteral("SupportsSyncPlayPlaybackRate")).toBool());
  // Remote control is handled (jfd::remote), and the commands are as given.
  CHECK(body.value(QStringLiteral("SupportsMediaControl")).toBool());
  CHECK(body.value(QStringLiteral("SupportedCommands")).toArray() == QJsonArray{QStringLiteral("DisplayMessage")});
  CHECK(body.value(QStringLiteral("PlayableMediaTypes")).toArray().size() == 2);
  REQUIRE(client.capabilities());
  CHECK(client.capabilities()->supportsSyncPlayPlaybackRate);
}

TEST_CASE("The playback-rate probe tells the extension from a stock server") {
  int status = 403;
  FakeServer server([&](const HttpRequest&) { return HttpResponse{status, {}, {}, 0}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));

  for (int answer : {400, 403}) {
    status = answer;
    auto probe = run<bool>([&](auto done) { client.probeSyncPlayPlaybackRate(nullptr, done); });
    REQUIRE(*probe);
    CHECK(probe->value());
  }
  for (int answer : {404, 405}) {
    status = answer;
    auto probe = run<bool>([&](auto done) { client.probeSyncPlayPlaybackRate(nullptr, done); });
    REQUIRE(*probe);
    CHECK_FALSE(probe->value());
  }
  const auto body = QJsonDocument::fromJson(server.requests().at(0).body).object();
  CHECK(body.value(QStringLiteral("PlaybackRate")).toDouble() == 0.0);
}

TEST_CASE("A SyncPlay 409 carries the problem code and participant count") {
  FakeServer server([](const HttpRequest&) {
    return jfd::test::json(
        R"({"status": 409, "title": "SyncPlay playback rate is not supported by every participant",
      "code": "SyncPlayPlaybackRateNotSupported", "unsupportedParticipants": 2})",
        409);
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));
  SyncPlayApi syncPlay(client);
  auto result = run<Empty>([&](auto done) { syncPlay.setPlaybackRate(1.5, nullptr, done); });
  REQUIRE_FALSE(*result);
  CHECK(result->error().isPlaybackRateConflict());
  CHECK(result->error().unsupportedParticipants() == 2);
  CHECK(QJsonDocument::fromJson(server.requests().at(0).body)
            .object()
            .value(QStringLiteral("PlaybackRate"))
            .toDouble() == doctest::Approx(1.5));
}

TEST_CASE("SetPlaybackRate on a server without the route is Unsupported") {
  FakeServer server([](const HttpRequest&) { return HttpResponse{405, {}, {}, 0}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  SyncPlayApi syncPlay(client);
  auto result = run<Empty>([&](auto done) { syncPlay.setPlaybackRate(1.25, nullptr, done); });
  REQUIRE_FALSE(*result);
  CHECK(result->error().kind == ApiError::Kind::Unsupported);
}

TEST_CASE("Ready sends server time with seven fractional digits and the queue occurrence") {
  FakeServer server([](const HttpRequest&) { return HttpResponse{204, {}, {}, 0}; });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  SyncPlayApi syncPlay(client);
  SyncPlayReadyState state;
  state.when = *parseServerInstant(u"2026-10-20T07:05:02.1234567Z");
  state.positionTicks = 600'000'000;
  state.isPlaying = true;
  state.playlistItemId = QStringLiteral("pl1");
  auto result = run<Empty>([&](auto done) { syncPlay.ready(state, nullptr, done); });
  REQUIRE(*result);
  const auto body = QJsonDocument::fromJson(server.requests().at(0).body).object();
  CHECK(server.requests().at(0).path == QStringLiteral("/SyncPlay/Ready"));
  CHECK(body.value(QStringLiteral("When")).toString() ==
        QStringLiteral("2026-10-20T07:05:02.1234567Z"));
  CHECK(body.value(QStringLiteral("PositionTicks")).toInteger() == 600'000'000);
  CHECK(body.value(QStringLiteral("IsPlaying")).toBool());
  CHECK(body.value(QStringLiteral("PlaylistItemId")).toString() == QStringLiteral("pl1"));
}

TEST_CASE("GetUtcTime samples bracket the request with the session clock") {
  FakeServer server([](const HttpRequest&) {
    return jfd::test::json(R"({"RequestReceptionTime": "2026-10-20T07:05:02.0000001Z",
                              "ResponseTransmissionTime": "2026-10-20T07:05:02.0000002Z"})");
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  SyncPlayApi syncPlay(client);
  FixedClock clock;
  auto sample = run<UtcTimeSample>([&](auto done) { syncPlay.utcTime(clock, nullptr, done); });
  REQUIRE(*sample);
  CHECK((*sample)->requestSent.sinceOrigin().count() == 1000);
  CHECK((*sample)->responseReceived.sinceOrigin().count() == 2000);
  CHECK(((*sample)->responseTransmission - (*sample)->requestReception).count() == 1);
}

TEST_CASE("SyncPlay groups list and create") {
  FakeServer server([](const HttpRequest& r) -> HttpResponse {
    if (r.path == QLatin1String("/SyncPlay/List"))
      return jfd::test::json(R"([{"GroupId": "g1", "GroupName": "Movie night", "State": "Idle",
        "Participants": ["alice"], "LastUpdatedAt": "2026-10-20T07:00:00Z"}])");
    if (r.path == QLatin1String("/SyncPlay/New"))
      return jfd::test::json(
          R"({"GroupId": "g2", "GroupName": "New", "State": "Idle", "Participants": []})");
    return {404, {}, {}, 0};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  SyncPlayApi syncPlay(client);
  auto groups = run<QList<SyncPlayGroup>>([&](auto done) { syncPlay.listGroups(nullptr, done); });
  REQUIRE(*groups);
  REQUIRE((*groups)->size() == 1);
  CHECK((*groups)->front().groupName == QStringLiteral("Movie night"));
  CHECK((*groups)->front().lastUpdatedAt.has_value());

  auto created = run<SyncPlayGroup>(
      [&](auto done) { syncPlay.createGroup(QStringLiteral("New"), nullptr, done); });
  REQUIRE(*created);
  CHECK((*created)->groupId == QStringLiteral("g2"));
}

TEST_CASE("Raw requests pass any answer through with the session's authorization") {
  FakeServer server([](const HttpRequest& r) -> HttpResponse {
    if (r.path == QLatin1String("/SyncPlay/SetPlaybackRate"))
      return jfd::test::json(R"({"code": "SyncPlayPlaybackRateNotSupported"})", 409);
    if (r.path == QLatin1String("/GetUtcTime"))
      return jfd::test::json(R"({"RequestReceptionTime": "x"})");
    return {404, {}, {}, 0};
  });
  JellyfinClient client(identity());
  client.setServerUrl(server.url());
  client.setSession(QStringLiteral("token1"), QStringLiteral("user1"));

  struct Answer {
    int status = -1;
    QByteArray body;
  };
  auto raw = [&](JellyfinClient::Method method, const QString& path, const QByteArray& body) {
    std::optional<Answer> out;
    client.rawRequest(method, path, body, nullptr,
                      [&out](int status, QByteArray data) { out = Answer{status, data}; });
    REQUIRE(waitFor([&] { return out.has_value(); }));
    return *out;
  };

  const Answer conflict =
      raw(JellyfinClient::Method::Post, QStringLiteral("/SyncPlay/SetPlaybackRate"),
          R"({"PlaybackRate":1.5})");
  CHECK(conflict.status == 409);
  CHECK(conflict.body.contains("SyncPlayPlaybackRateNotSupported"));
  const HttpRequest& sent = server.requests().at(0);
  CHECK(sent.method == "POST");
  CHECK(sent.body == R"({"PlaybackRate":1.5})");
  CHECK(sent.headers.value("content-type") == "application/json");
  CHECK(sent.headers.value("authorization").contains("Token=\"token1\""));

  const Answer time = raw(JellyfinClient::Method::Get, QStringLiteral("/GetUtcTime?a=1"), {});
  CHECK(time.status == 200);
  CHECK(time.body.contains("RequestReceptionTime"));
  CHECK(server.requests().at(1).method == "GET");
  CHECK(server.requests().at(1).query.queryItemValue(QStringLiteral("a")) == QStringLiteral("1"));

  client.setServerUrl(QUrl(QStringLiteral("http://127.0.0.1:1")));
  CHECK(raw(JellyfinClient::Method::Get, QStringLiteral("/GetUtcTime"), {}).status == 0);
}
