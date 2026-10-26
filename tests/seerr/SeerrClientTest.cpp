#include "../support/DoctestQt.h"

#include "../support/FakeSeerr.h"

#include <jfd/platform/CredentialStore.h>
#include <jfd/seerr/SeerrClient.h>
#include <jfd/seerr/SessionStore.h>

#include <QSignalSpy>

#include <optional>

using namespace jfd::seerr;
using jfd::api::ApiError;
using jfd::api::Empty;
using jfd::api::Result;
using jfd::test::FakeSeerr;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::waitFor;

namespace {

  template <class T> std::optional<Result<T>> run(std::function<void(Callback<T>)> call) {
    std::optional<Result<T>> out;
    call([&out](Result<T> r) { out.emplace(std::move(r)); });
    REQUIRE(waitFor([&] { return out.has_value(); }));
    return out;
  }

  std::unique_ptr<SeerrClient> clientFor(FakeSeerr& seerr) {
    auto client = std::make_unique<SeerrClient>();
    client->setServerUrl(seerr.url());
    return client;
  }

  Result<User> signIn(SeerrClient& client, const char* name = "alice", const char* password = "pw-alice") {
    return *run<User>([&](Callback<User> cb) {
      client.signIn(QString::fromLatin1(name), QString::fromLatin1(password), &client, std::move(cb));
    });
  }

  int count(const FakeSeerr& seerr, const char* method, const QString& path) {
    int n = 0;
    for (const HttpRequest& r : seerr.server().requests()) {
      if (r.method == method && r.path == path) ++n;
    }
    return n;
  }

} // namespace

TEST_CASE("Server addresses are normalized and cleartext over the internet is told apart") {
  CHECK(SeerrClient::normalizeServerUrl(QStringLiteral("seerr.lan:5055"))->toString() ==
        QStringLiteral("http://seerr.lan:5055"));
  CHECK(SeerrClient::normalizeServerUrl(QStringLiteral(" https://x.example.org/seerr/ "))->toString() ==
        QStringLiteral("https://x.example.org/seerr"));
  CHECK(SeerrClient::normalizeServerUrl(QStringLiteral("https://u:p@x.example.org/?a=b#c"))->toString() ==
        QStringLiteral("https://x.example.org"));
  CHECK_FALSE(SeerrClient::normalizeServerUrl(QStringLiteral("ftp://x.example.org")));
  CHECK_FALSE(SeerrClient::normalizeServerUrl(QString()));

  const auto insecure = [](const char* url) {
    return SeerrClient::isCleartextOverInternet(QUrl(QString::fromLatin1(url)));
  };
  CHECK(insecure("http://seerr.example.org:5055"));
  CHECK(insecure("http://8.8.8.8"));
  CHECK_FALSE(insecure("https://seerr.example.org"));
  CHECK_FALSE(insecure("http://localhost:5055"));
  CHECK_FALSE(insecure("http://127.0.0.1:5055"));
  CHECK_FALSE(insecure("http://192.168.1.50:5055"));
  CHECK_FALSE(insecure("http://10.0.0.2"));
  CHECK_FALSE(insecure("http://172.20.1.1"));
  CHECK(insecure("http://172.32.1.1"));
  CHECK_FALSE(insecure("http://seerr"));
  CHECK_FALSE(insecure("http://seerr.local"));
  CHECK_FALSE(insecure("http://[::1]:5055"));
  CHECK_FALSE(insecure("http://[fd00::5]:5055"));
}

TEST_CASE("Sign-in as the Jellyfin user keeps only the session cookie") {
  FakeSeerr seerr;
  auto client = clientFor(seerr);
  QSignalSpy changed(client.get(), &SeerrClient::sessionChanged);

  const auto user = signIn(*client);
  REQUIRE(user.ok());
  CHECK(user->id == 7);
  CHECK(user->name == QStringLiteral("alice"));
  CHECK(user->permissions == 32);
  CHECK(client->isSignedIn());
  CHECK(client->userId() == 7);
  CHECK(changed.count() == 1);
  // The probe came first, then the password.
  CHECK(count(seerr, "GET", QStringLiteral("/api/v1/status")) == 1);
  CHECK(count(seerr, "POST", QStringLiteral("/api/v1/auth/jellyfin")) == 1);
  // The cookie is the session, not the Jellyfin token or the password.
  CHECK(client->sessionCookie().startsWith(QStringLiteral("s%3A")));
  CHECK_FALSE(client->sessionCookie().contains(seerr.jellyfinToken));
  CHECK_FALSE(client->sessionCookie().contains(QStringLiteral("pw-alice")));
  // No API key header is ever sent.
  for (const HttpRequest& r : seerr.server().requests())
    CHECK_FALSE(r.headers.contains("x-api-key"));

  const auto me = *run<User>([&](Callback<User> cb) { client->currentUser(client.get(), std::move(cb)); });
  REQUIRE(me.ok());
  CHECK(me->id == 7);
}

TEST_CASE("A wrong password, a server error or a failed sign-in leaves nobody signed in") {
  FakeSeerr seerr;
  auto client = clientFor(seerr);

  const auto wrong = signIn(*client, "alice", "nope");
  REQUIRE_FALSE(wrong.ok());
  CHECK(refusalOf(wrong.error(), true) == Refusal::BadLogin);
  CHECK_FALSE(client->isSignedIn());

  const auto unknown = signIn(*client, "ghost", "x");
  REQUIRE_FALSE(unknown.ok());
  CHECK(refusalOf(unknown.error(), true) == Refusal::BadLogin);

  seerr.loginFails500 = true;
  const auto broken = signIn(*client);
  REQUIRE_FALSE(broken.ok());
  CHECK(refusalOf(broken.error(), true) == Refusal::BadLogin);
  CHECK_FALSE(client->isSignedIn());
}

TEST_CASE("The password is not sent to a server that is not a Seerr with Jellyfin") {
  {
    FakeSeerr seerr;
    seerr.mediaServerType = 1; // Plex
    auto client = clientFor(seerr);
    const auto result = signIn(*client);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ApiError::Kind::Unsupported);
    CHECK(count(seerr, "POST", QStringLiteral("/api/v1/auth/jellyfin")) == 0);
  }
  {
    FakeSeerr seerr;
    seerr.initialized = false;
    auto client = clientFor(seerr);
    CHECK_FALSE(signIn(*client).ok());
    CHECK(count(seerr, "POST", QStringLiteral("/api/v1/auth/jellyfin")) == 0);
  }
  {
    // Something else on that address: the probe fails, nothing is sent.
    FakeServer other([](const HttpRequest&) { return jfd::test::json("{\"hello\":1}"); });
    SeerrClient client;
    client.setServerUrl(other.url());
    const auto result = *run<User>([&](Callback<User> cb) {
      client.signIn(QStringLiteral("alice"), QStringLiteral("pw-alice"), &client, std::move(cb));
    });
    CHECK_FALSE(result.ok());
    for (const HttpRequest& r : other.requests())
      CHECK(r.method == "GET");
  }
}

TEST_CASE("A redirect is not followed, so neither the password nor the cookie leaves the host") {
  FakeServer elsewhere([](const HttpRequest&) { return jfd::test::json("{}"); });
  FakeSeerr seerr;
  seerr.redirectStatusTo = elsewhere.url(QStringLiteral("/api/v1/status")).toString();
  auto client = clientFor(seerr);

  const auto result = signIn(*client);
  REQUIRE_FALSE(result.ok());
  CHECK(result.error().httpStatus == 302);
  CHECK(elsewhere.requests().isEmpty());
  CHECK(count(seerr, "POST", QStringLiteral("/api/v1/auth/jellyfin")) == 0);

  // With a session, a redirect still carries nothing along.
  seerr.redirectStatusTo.clear();
  REQUIRE(signIn(*client).ok());
  seerr.redirectStatusTo = elsewhere.url(QStringLiteral("/x")).toString();
  const auto probe = *run<ServerInfo>([&](Callback<ServerInfo> cb) { client->probe(client.get(), std::move(cb)); });
  CHECK_FALSE(probe.ok());
  CHECK(elsewhere.requests().isEmpty());
}

TEST_CASE("Browse, search and title pages parse what Seerr sends") {
  FakeSeerr seerr;
  auto client = clientFor(seerr);
  REQUIRE(signIn(*client).ok());

  const auto movies = *run<TitlePage>([&](Callback<TitlePage> cb) { client->discoverMovies(1, client.get(), std::move(cb)); });
  REQUIRE(movies.ok());
  REQUIRE(movies->results.size() == 2);
  CHECK(movies->results[0].tmdbId == 603);
  CHECK(movies->results[0].title == QStringLiteral("The Matrix"));
  CHECK(movies->results[0].year() == 2021);
  CHECK(movies->results[0].status == MediaStatus::Available);
  CHECK_FALSE(movies->results[0].canRequest());
  CHECK(movies->results[1].status == MediaStatus::None);
  CHECK(movies->results[1].canRequest());
  CHECK(movies->totalPages == 2);

  const auto shows = *run<TitlePage>([&](Callback<TitlePage> cb) { client->discoverTv(1, client.get(), std::move(cb)); });
  REQUIRE(shows.ok());
  CHECK(shows->results[0].type == MediaType::Tv);
  CHECK(shows->results[0].title == QStringLiteral("Thrones"));
  CHECK(shows->results[0].status == MediaStatus::PartiallyAvailable);
  CHECK(shows->results[0].canRequest());

  // People are dropped from the mix.
  const auto trending = *run<TitlePage>([&](Callback<TitlePage> cb) { client->trending(1, client.get(), std::move(cb)); });
  REQUIRE(trending.ok());
  CHECK(trending->results.size() == 2);
  CHECK(trending->results[1].status == MediaStatus::Pending);
  CHECK_FALSE(trending->results[1].canRequest());

  const auto found = *run<TitlePage>([&](Callback<TitlePage> cb) {
    client->search(QStringLiteral("a b&c=d"), 2, client.get(), std::move(cb));
  });
  REQUIRE(found.ok());
  CHECK(found->results[0].title == QStringLiteral("Found a b&c=d"));

  const auto movie = *run<TitleDetails>([&](Callback<TitleDetails> cb) { client->movie(603, client.get(), std::move(cb)); });
  REQUIRE(movie.ok());
  CHECK(movie->runtimeMinutes == 136);
  CHECK(movie->genres == QStringList{QStringLiteral("Action")});
  CHECK(movie->title.status == MediaStatus::Available);

  const auto show = *run<TitleDetails>([&](Callback<TitleDetails> cb) { client->tv(1399, client.get(), std::move(cb)); });
  REQUIRE(show.ok());
  REQUIRE(show->seasons.size() == 2); // specials left out
  CHECK(show->seasons[0].number == 1);
  CHECK(show->seasons[0].status == MediaStatus::Available);
  CHECK(show->seasons[1].status == MediaStatus::None);
  CHECK(show->runtimeMinutes == 55);
  REQUIRE(show->requests.size() == 1);
  CHECK(show->requests[0].status == RequestStatus::Approved);
  CHECK(show->requests[0].requestedBy == 7);
}

TEST_CASE("Requests: make, refuse by role, refuse duplicates, list and cancel") {
  FakeSeerr seerr;
  auto client = clientFor(seerr);
  REQUIRE(signIn(*client).ok());

  const auto movie = *run<UserRequest>([&](Callback<UserRequest> cb) {
    client->request(MediaType::Movie, 604, {}, client.get(), std::move(cb));
  });
  REQUIRE(movie.ok());
  CHECK(movie->status == RequestStatus::PendingApproval);
  CHECK(movie->tmdbId == 604);
  CHECK(seerr.lastRequestBody().value("mediaType").toString() == QStringLiteral("movie"));
  CHECK_FALSE(seerr.lastRequestBody().contains("seasons"));
  // Seerr's defaults apply: nothing about 4K, profiles or folders is sent.
  CHECK(seerr.lastRequestBody().keys().size() == 2);

  const auto again = *run<UserRequest>([&](Callback<UserRequest> cb) {
    client->request(MediaType::Movie, 604, {}, client.get(), std::move(cb));
  });
  REQUIRE_FALSE(again.ok());
  CHECK(refusalOf(again.error()) == Refusal::AlreadyDone);

  const auto all = *run<UserRequest>([&](Callback<UserRequest> cb) {
    client->request(MediaType::Tv, 1400, SeasonSelection{true, {}}, client.get(), std::move(cb));
  });
  REQUIRE(all.ok());
  CHECK(seerr.lastRequestBody().value("seasons").toString() == QStringLiteral("all"));

  const auto some = *run<UserRequest>([&](Callback<UserRequest> cb) {
    client->request(MediaType::Tv, 1401, SeasonSelection{false, {1, 3}}, client.get(), std::move(cb));
  });
  REQUIRE(some.ok());
  CHECK(some->seasons == QList<int>{1, 3});
  CHECK(seerr.lastRequestBody().value("seasons").toArray().size() == 2);

  const auto mine = *run<RequestPage>([&](Callback<RequestPage> cb) { client->myRequests(20, 0, client.get(), std::move(cb)); });
  REQUIRE(mine.ok());
  CHECK(mine->results.size() == 3);

  const auto cancelled = *run<Empty>([&](Callback<Empty> cb) { client->cancelRequest(some->id, client.get(), std::move(cb)); });
  CHECK(cancelled.ok());
  const auto after = *run<RequestPage>([&](Callback<RequestPage> cb) { client->myRequests(20, 0, client.get(), std::move(cb)); });
  CHECK(after->results.size() == 2);
  const auto missing = *run<Empty>([&](Callback<Empty> cb) { client->cancelRequest(9999, client.get(), std::move(cb)); });
  REQUIRE_FALSE(missing.ok());
  CHECK(refusalOf(missing.error()) == Refusal::NotFound);

  // An account without the right to request is refused with a plain 403.
  auto viewer = clientFor(seerr);
  REQUIRE(signIn(*viewer, "viewer", "pw-viewer").ok());
  const auto refused = *run<UserRequest>([&](Callback<UserRequest> cb) {
    viewer->request(MediaType::Movie, 700, {}, viewer.get(), std::move(cb));
  });
  REQUIRE_FALSE(refused.ok());
  CHECK(refusalOf(refused.error()) == Refusal::NoPermission);
  CHECK(viewer->isSignedIn()); // a refusal is not a lost session
}

TEST_CASE("A lost session is reported once and dropped; sign-out clears it") {
  FakeSeerr seerr;
  auto client = clientFor(seerr);
  REQUIRE(signIn(*client).ok());
  QSignalSpy expired(client.get(), &SeerrClient::sessionExpired);

  seerr.sessions().clear();
  const auto trending = *run<TitlePage>([&](Callback<TitlePage> cb) { client->trending(1, client.get(), std::move(cb)); });
  REQUIRE_FALSE(trending.ok());
  CHECK(refusalOf(trending.error()) == Refusal::SignedOut);
  CHECK(expired.count() == 1);
  CHECK_FALSE(client->isSignedIn());

  // Signed out locally: requests that need a user id say so without a round trip.
  const auto mine = *run<RequestPage>([&](Callback<RequestPage> cb) { client->myRequests(20, 0, client.get(), std::move(cb)); });
  CHECK(refusalOf(mine.error()) == Refusal::SignedOut);

  REQUIRE(signIn(*client).ok());
  const auto out = *run<Empty>([&](Callback<Empty> cb) { client->signOut(client.get(), std::move(cb)); });
  CHECK(out.ok());
  CHECK_FALSE(client->isSignedIn());
  CHECK(expired.count() == 1);
}

TEST_CASE("A call whose owner went away never reports back") {
  FakeSeerr seerr;
  auto client = clientFor(seerr);
  REQUIRE(signIn(*client).ok());
  bool called = false;
  {
    QObject owner;
    client->trending(1, &owner, [&](Result<TitlePage>) { called = true; });
  }
  // Let the answer arrive.
  QCoreApplication::processEvents();
  waitFor([&] { return false; }, 150);
  CHECK_FALSE(called);
}

TEST_CASE("An unreachable server is told apart from a refusal") {
  SeerrClient client;
  client.setServerUrl(QUrl(QStringLiteral("http://127.0.0.1:1")));
  client.setTimeoutMs(2000);
  const auto result = *run<ServerInfo>([&](Callback<ServerInfo> cb) { client.probe(&client, std::move(cb)); });
  REQUIRE_FALSE(result.ok());
  CHECK(refusalOf(result.error()) == Refusal::Unreachable);
  // The error text names no address.
  CHECK_FALSE(result.error().message.contains(QStringLiteral("127.0.0.1")));
}

TEST_CASE("The session store keeps the cookie per address and nothing else") {
  jfd::platform::MemoryCredentialStore memory;
  SessionStore store(memory);
  const QUrl a(QStringLiteral("https://seerr.example.org"));
  const QUrl b(QStringLiteral("https://other.example.org"));

  CHECK(store.load(a).cookie.isEmpty());
  CHECK(store.save(a, QStringLiteral("s%3Aabc.def"), 7));
  CHECK(store.load(a).cookie == QStringLiteral("s%3Aabc.def"));
  CHECK(store.load(a).userId == 7);
  CHECK(store.load(b).cookie.isEmpty());
  CHECK_FALSE(store.save(a, QString(), 7));
  CHECK_FALSE(store.save(a, QStringLiteral("x"), 0));
  // The key names the address but never holds the cookie.
  CHECK_FALSE(SessionStore::key(a).contains(QStringLiteral("abc")));
  store.clear(a);
  CHECK(store.load(a).cookie.isEmpty());
}

TEST_CASE("Model values from Seerr's numbers") {
  CHECK(json::mediaStatus(5) == MediaStatus::Available);
  CHECK(json::mediaStatus(99) == MediaStatus::None);
  CHECK(json::requestStatus(3) == RequestStatus::Declined);
  CHECK(json::requestStatus(0) == RequestStatus::None);
  CHECK_FALSE(json::title(QJsonObject{{"id", 1}, {"mediaType", "person"}, {"name", "x"}}));
  CHECK_FALSE(json::title(QJsonObject{{"mediaType", "movie"}, {"title", "no id"}}));
  const auto fallback = json::title(QJsonObject{{"id", 4}, {"title", "Bare"}}, MediaType::Movie);
  REQUIRE(fallback);
  CHECK(fallback->title == QStringLiteral("Bare"));
}
