#include "LibraryTestSupport.h"

#include <jfd/library/SignIn.h>
#include <jfd/platform/CredentialStore.h>

#include <QSignalSpy>
#include <QTemporaryDir>

using namespace jfd;
using jfd::library::ServerStore;
using jfd::library::SignIn;
using jfd::platform::MemoryCredentialStore;
using namespace jfd::test;

namespace {
  const QByteArray Info =
      R"({"Id": "srv1", "ServerName": "Home", "Version": "12.2.0", "StartupWizardCompleted": true})";
  const QByteArray Auth = R"({"AccessToken": "tok-new", "ServerId": "srv1",
    "User": {"Id": "u1", "Name": "alice", "ServerId": "srv1"}})";

  struct Server {
    bool quickConnect = true;
    int quickConnectPolls = 0;
    bool rejectToken = false;
    QString password = QStringLiteral("secret");

    HttpResponse operator()(const HttpRequest& r) {
      const QString p = r.path;
      if (p == QLatin1String("/System/Info/Public")) return ok(Info);
      if (p == QLatin1String("/QuickConnect/Enabled")) return ok(quickConnect ? "true" : "false");
      if (p == QLatin1String("/Users/AuthenticateByName")) {
        const auto body = QJsonDocument::fromJson(r.body).object();
        if (body.value(QStringLiteral("Pw")).toString() != password) return {401, {}, {}, 0};
        return ok(Auth);
      }
      if (p == QLatin1String("/QuickConnect/Initiate"))
        return ok(R"({"Secret": "s3cr3t", "Code": "123456", "Authenticated": false})");
      if (p == QLatin1String("/QuickConnect/Connect"))
        return ok(++quickConnectPolls >= 2
                      ? R"({"Secret": "s3cr3t", "Code": "123456", "Authenticated": true})"
                      : R"({"Secret": "s3cr3t", "Code": "123456", "Authenticated": false})");
      if (p == QLatin1String("/Users/AuthenticateWithQuickConnect")) return ok(Auth);
      if (p == QLatin1String("/Users/Me"))
        return rejectToken ? HttpResponse{401, {}, {}, 0}
                           : ok(R"({"Id": "u1", "Name": "alice2", "ServerId": "srv1"})");
      if (p == QLatin1String("/Sessions/Logout")) return HttpResponse{204, {}, {}, 0};
      if (p == QLatin1String("/Items"))
        return rejectToken ? HttpResponse{401, {}, {}, 0} : ok(page(""));
      return {404, {}, {}, 0};
    }
  };

  struct Setup {
    Server routes;
    FakeServer server{[this](const HttpRequest& r) { return routes(r); }};
    api::JellyfinClient client{api::ClientIdentity{}};
    QTemporaryDir dir;
    ServerStore servers{dir.filePath(QStringLiteral("servers.ini"))};
    MemoryCredentialStore tokens;
    SignIn signIn{client, servers, tokens};
    Setup() { signIn.setQuickConnectPollMs(20); }
  };
} // namespace

TEST_CASE("Password sign-in checks the server, then stores the token and the server") {
  Setup s;
  QSignalSpy signedIn(&s.signIn, &SignIn::signedIn);
  CHECK_FALSE(s.signIn.restore());

  s.signIn.connectToServer(QStringLiteral("not a url ::"));
  CHECK_FALSE(s.signIn.errorText().isEmpty());

  s.signIn.connectToServer(s.server.url().toString() + QStringLiteral("/web/"));
  CHECK(s.signIn.step() == SignIn::Step::CheckingServer);
  CHECK(s.signIn.busy());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  CHECK(s.signIn.serverName() == QStringLiteral("Home"));
  CHECK(s.signIn.quickConnectAvailable());

  s.signIn.signIn(QStringLiteral("alice"), QStringLiteral("wrong"));
  REQUIRE(waitFor([&] {
    return s.signIn.step() == SignIn::Step::Credentials && !s.signIn.busy() &&
           !s.signIn.errorText().isEmpty();
  }));
  CHECK(s.signIn.errorText() == QStringLiteral("Wrong user name or password."));

  s.signIn.signIn(QStringLiteral(" alice "), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
  CHECK(signedIn.count() == 1);
  CHECK(s.signIn.userName() == QStringLiteral("alice"));
  CHECK(s.signIn.tokenSaved());
  CHECK(s.client.accessToken() == QStringLiteral("tok-new"));
  CHECK(s.tokens.read(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1")))
            .secret == QStringLiteral("tok-new"));
  const auto active = s.servers.active();
  REQUIRE(active);
  CHECK(active->url == s.server.url());
  CHECK(active->userName == QStringLiteral("alice"));
  CHECK(s.signIn.savedServers().size() == 1);
}

TEST_CASE("A saved sign-in is restored at start-up and checked afterwards") {
  Setup s;
  library::SavedServer saved;
  saved.id = QStringLiteral("srv1");
  saved.name = QStringLiteral("Home");
  saved.url = s.server.url();
  saved.userId = QStringLiteral("u1");
  saved.userName = QStringLiteral("alice");
  s.servers.save(saved);
  s.tokens.write(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1")),
                 QStringLiteral("tok-old"));

  REQUIRE(s.signIn.restore());
  CHECK(s.signIn.step() == SignIn::Step::SignedIn);
  CHECK(s.client.accessToken() == QStringLiteral("tok-old"));
  CHECK(s.client.serverUrl() == s.server.url());
  // The server's current user name replaces the saved one.
  REQUIRE(waitFor([&] { return s.signIn.userName() == QLatin1String("alice2"); }));
  CHECK(s.servers.active()->userName == QStringLiteral("alice2"));
}

TEST_CASE("A rejected token brings back the password form for the same server") {
  Setup s;
  s.routes.rejectToken = true;
  library::SavedServer saved{QStringLiteral("srv1"), QStringLiteral("Home"),  s.server.url(),
                             QStringLiteral("u1"),   QStringLiteral("alice"), {}};
  s.servers.save(saved);
  const QString key = platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1"));
  s.tokens.write(key, QStringLiteral("tok-old"));
  QSignalSpy signedOut(&s.signIn, &SignIn::signedOut);

  REQUIRE(s.signIn.restore());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  CHECK(signedOut.count() == 1);
  CHECK(s.signIn.serverName() == QStringLiteral("Home"));
  CHECK_FALSE(s.signIn.errorText().isEmpty());
  CHECK(s.tokens.read(key).status == platform::CredentialStore::Status::NotFound);
  CHECK(s.servers.active()->userId.isEmpty());
  CHECK_FALSE(s.signIn.restore());

  // Signing in again works from here.
  s.routes.rejectToken = false;
  s.signIn.signIn(QStringLiteral("alice"), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
}

TEST_CASE("Quick Connect shows a code, waits for approval and signs in") {
  Setup s;
  s.signIn.connectToServer(s.server.url().toString());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  s.signIn.startQuickConnect();
  REQUIRE(waitFor([&] { return s.signIn.quickConnectCode() == QLatin1String("123456"); }));
  CHECK(s.signIn.step() == SignIn::Step::QuickConnect);
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
  CHECK(s.routes.quickConnectPolls >= 2);
  CHECK(s.client.accessToken() == QStringLiteral("tok-new"));
  CHECK(s.signIn.quickConnectCode().isEmpty());
}

TEST_CASE("Quick Connect can be cancelled and is not offered when the server has it off") {
  Setup s;
  s.signIn.connectToServer(s.server.url().toString());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  s.signIn.startQuickConnect();
  REQUIRE(waitFor([&] { return !s.signIn.quickConnectCode().isEmpty(); }));
  s.signIn.cancelQuickConnect();
  CHECK(s.signIn.step() == SignIn::Step::Credentials);
  waitFor([] { return false; }, 100);
  CHECK(s.signIn.step() == SignIn::Step::Credentials);

  s.routes.quickConnect = false;
  s.signIn.back();
  CHECK(s.signIn.step() == SignIn::Step::ChooseServer);
  s.signIn.connectToServer(s.server.url().toString());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  CHECK_FALSE(s.signIn.quickConnectAvailable());
  s.signIn.startQuickConnect();
  CHECK(s.signIn.step() == SignIn::Step::Credentials);
}

TEST_CASE("Signing out revokes the token and keeps the server; forgetting removes it") {
  Setup s;
  s.signIn.connectToServer(s.server.url().toString());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  s.signIn.signIn(QStringLiteral("alice"), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));

  s.signIn.signOut();
  CHECK(s.signIn.step() == SignIn::Step::Credentials);
  REQUIRE(waitFor(
      [&] { return s.server.requests().last().path == QLatin1String("/Sessions/Logout"); }));
  CHECK(s.tokens.read(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1")))
            .status == platform::CredentialStore::Status::NotFound);
  REQUIRE(s.servers.servers().size() == 1);
  CHECK(s.servers.servers().front().userId.isEmpty());

  s.signIn.forgetServer(QStringLiteral("srv1"));
  CHECK(s.servers.servers().isEmpty());
  CHECK_FALSE(s.servers.active());
}

TEST_CASE("Saved servers are listed most recent first") {
  QTemporaryDir dir;
  ServerStore store(dir.filePath(QStringLiteral("s.ini")));
  library::SavedServer a{QStringLiteral("a"),
                         QStringLiteral("A"),
                         QUrl(QStringLiteral("http://a")),
                         {},
                         {},
                         QDateTime::currentDateTimeUtc().addDays(-1)};
  library::SavedServer b{QStringLiteral("b"),
                         QStringLiteral("B"),
                         QUrl(QStringLiteral("http://b")),
                         {},
                         {},
                         QDateTime::currentDateTimeUtc()};
  store.save(a);
  store.save(b);
  CHECK(store.servers().front().id == QStringLiteral("b"));
  CHECK(store.active()->id == QStringLiteral("b"));
  store.setActive(QStringLiteral("a"));
  CHECK(store.servers().front().id == QStringLiteral("a"));
  // Re-reading from the file gives the same.
  ServerStore again(dir.filePath(QStringLiteral("s.ini")));
  CHECK(again.active()->id == QStringLiteral("a"));
  CHECK(again.servers().size() == 2);
}
