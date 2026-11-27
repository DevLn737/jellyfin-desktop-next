#include "library/LibraryTestSupport.h"

#include <jfd/account/SignIn.h>
#include <jfd/platform/AsyncCredentialStore.h>
#include <jfd/platform/CredentialStore.h>
#include <chrono>
#include <thread>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace jfd;
using jfd::account::ServerStore;
using jfd::account::SignIn;
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
      if (p == QLatin1String("/Sessions/Playing/Stopped")) return HttpResponse{204, {}, {}, 200};
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
    platform::AsyncCredentialStore queue{tokens};
    SignIn signIn{client, servers, queue};
    Setup() { signIn.setQuickConnectPollMs(20); }
    // The store's calls made so far are done and answered.
    void settle() { REQUIRE(queue.finishPending(std::chrono::seconds(5))); }
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
  // The form comes back with the name that was typed.
  CHECK(s.signIn.userName() == QStringLiteral("alice"));

  // An empty name says so and stays on the form.
  s.signIn.signIn(QStringLiteral("  "), QStringLiteral("secret"));
  CHECK(s.signIn.step() == SignIn::Step::Credentials);
  CHECK(s.signIn.errorText() == QStringLiteral("Enter a user name."));

  s.signIn.signIn(QStringLiteral(" alice "), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
  CHECK(signedIn.count() == 1);
  CHECK(s.signIn.userName() == QStringLiteral("alice"));
  s.settle();
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
  account::SavedServer saved;
  saved.id = QStringLiteral("srv1");
  saved.name = QStringLiteral("Home");
  saved.url = s.server.url();
  saved.userId = QStringLiteral("u1");
  saved.userName = QStringLiteral("alice");
  s.servers.save(saved);
  s.tokens.write(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1")),
                 QStringLiteral("tok-old"));

  REQUIRE(s.signIn.restore());
  // The token is read off this thread.
  CHECK(s.signIn.step() == SignIn::Step::Restoring);
  CHECK(s.signIn.busy());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
  CHECK(s.signIn.tokenSaved());
  CHECK(s.client.accessToken() == QStringLiteral("tok-old"));
  CHECK(s.client.serverUrl() == s.server.url());
  // The server's current user name replaces the saved one.
  REQUIRE(waitFor([&] { return s.signIn.userName() == QLatin1String("alice2"); }));
  CHECK(s.servers.active()->userName == QStringLiteral("alice2"));
}

TEST_CASE("A rejected token brings back the password form for the same server") {
  Setup s;
  s.routes.rejectToken = true;
  account::SavedServer saved{QStringLiteral("srv1"), QStringLiteral("Home"),  s.server.url(),
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
  CHECK_FALSE(s.client.isSignedIn());
  // A 401 never deletes the saved token or user: only signing out does, or
  // the next sign-in replaces them.
  CHECK(s.tokens.read(key).secret == QStringLiteral("tok-old"));
  CHECK(s.servers.active()->userId == QStringLiteral("u1"));

  // Signing in again works from here, and replaces the kept token.
  s.routes.rejectToken = false;
  s.signIn.signIn(QStringLiteral("alice"), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
  s.settle();
  CHECK(s.tokens.read(key).secret == QStringLiteral("tok-new"));
}

TEST_CASE("A token refused for a moment is tried again and works") {
  Setup s;
  s.routes.rejectToken = true;
  account::SavedServer saved{QStringLiteral("srv1"), QStringLiteral("Home"),  s.server.url(),
                             QStringLiteral("u1"),   QStringLiteral("alice"), {}};
  s.servers.save(saved);
  const QString key = platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1"));
  s.tokens.write(key, QStringLiteral("tok-old"));
  REQUIRE(s.signIn.restore());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));

  // The proxy lets requests through again: the next start (or choosing the
  // server) signs in with the same token, no password needed.
  s.routes.rejectToken = false;
  REQUIRE(s.signIn.restore());
  s.settle();
  CHECK(s.client.accessToken() == QStringLiteral("tok-old"));
  waitFor([] { return false; }, 200);
  CHECK(s.signIn.step() == SignIn::Step::SignedIn);
}

namespace {
  // A keyring that takes its time (D-Bus, an unlock prompt).
  class SlowStore final : public platform::CredentialStore {
  public:
    explicit SlowStore(std::chrono::milliseconds delay) : m_delay(delay) {}
    bool isAvailable() const override { return true; }
    ReadResult read(const QString& key) const override {
      std::this_thread::sleep_for(m_delay);
      return m_store.read(key);
    }
    Status write(const QString& key, const QString& secret) override {
      std::this_thread::sleep_for(m_delay);
      return m_store.write(key, secret);
    }
    Status remove(const QString& key) override {
      std::this_thread::sleep_for(m_delay);
      return m_store.remove(key);
    }

  private:
    std::chrono::milliseconds m_delay;
    MemoryCredentialStore m_store;
  };

  account::SavedServer savedAlice(const QUrl& url) {
    return {QStringLiteral("srv1"), QStringLiteral("Home"),  url,
            QStringLiteral("u1"),   QStringLiteral("alice"), {}};
  }
} // namespace

TEST_CASE("A slow credential store never holds up the caller") {
  Setup s;
  SlowStore slow(std::chrono::milliseconds(400));
  platform::AsyncCredentialStore queue(slow);
  SignIn signIn(s.client, s.servers, queue);
  s.servers.save(savedAlice(s.server.url()));
  const QString key = platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1"));
  queue.write(key, QStringLiteral("tok-old"));

  QElapsedTimer timer;
  timer.start();
  REQUIRE(signIn.restore());
  CHECK(timer.elapsed() < 100);
  CHECK(signIn.step() == SignIn::Step::Restoring);
  REQUIRE(waitFor([&] { return signIn.step() == SignIn::Step::SignedIn; }, 5000));
  CHECK(s.client.accessToken() == QStringLiteral("tok-old"));

  // Signing out and in again: each answer of the store waits 400 ms.
  signIn.signOut();
  REQUIRE(waitFor([&] { return signIn.step() == SignIn::Step::Credentials; }));
  timer.restart();
  signIn.signIn(QStringLiteral("alice"), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return signIn.step() == SignIn::Step::SignedIn; }, 5000));
  CHECK_FALSE(signIn.tokenSaved());
  REQUIRE(waitFor([&] { return signIn.tokenSaved(); }, 5000));
  // The removal at sign-out went first, then the new token.
  REQUIRE(queue.finishPending(std::chrono::seconds(5)));
  CHECK(slow.read(key).secret == QStringLiteral("tok-new"));
}

TEST_CASE("A saved user without a stored token goes back to the server choice at start-up") {
  Setup s;
  s.servers.save(savedAlice(s.server.url()));
  QSignalSpy signedIn(&s.signIn, &SignIn::signedIn);
  REQUIRE(s.signIn.restore());
  CHECK(s.signIn.step() == SignIn::Step::Restoring);
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::ChooseServer; }));
  CHECK(signedIn.isEmpty());
  CHECK_FALSE(s.client.isSignedIn());
}

TEST_CASE("Choosing a saved server signs in with its token, or asks for the password") {
  Setup s;
  s.servers.save(savedAlice(s.server.url()));
  const QString key = platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1"));

  // No token: the password form of that server, with the saved name.
  s.signIn.chooseSavedServer(QStringLiteral("srv1"));
  CHECK(s.signIn.step() == SignIn::Step::Restoring);
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  CHECK(s.signIn.serverName() == QStringLiteral("Home"));
  CHECK(s.signIn.userName() == QStringLiteral("alice"));

  s.tokens.write(key, QStringLiteral("tok-old"));
  s.signIn.back();
  s.signIn.chooseSavedServer(QStringLiteral("srv1"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
  CHECK(s.client.accessToken() == QStringLiteral("tok-old"));
}

TEST_CASE("A server typed while the saved token is read wins over it") {
  Setup s;
  SlowStore slow(std::chrono::milliseconds(300));
  platform::AsyncCredentialStore queue(slow);
  SignIn signIn(s.client, s.servers, queue);
  s.servers.save(savedAlice(s.server.url()));
  queue.write(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1")),
              QStringLiteral("tok-old"));
  REQUIRE(signIn.restore());
  signIn.connectToServer(s.server.url().toString());
  REQUIRE(waitFor([&] { return signIn.step() == SignIn::Step::Credentials; }, 5000));
  REQUIRE(queue.finishPending(std::chrono::seconds(5)));
  waitFor([] { return false; }, 100);
  CHECK(signIn.step() == SignIn::Step::Credentials);
  CHECK_FALSE(s.client.isSignedIn());
}

TEST_CASE("Signing in as another user removes the token kept for the expired one") {
  Setup s;
  account::SavedServer saved{QStringLiteral("srv1"), QStringLiteral("Home"),  s.server.url(),
                             QStringLiteral("u0"),   QStringLiteral("bob"), {}};
  s.servers.save(saved);
  const QString oldKey = platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u0"));
  s.tokens.write(oldKey, QStringLiteral("tok-bob"));
  s.signIn.connectToServer(s.server.url().toString());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  s.signIn.signIn(QStringLiteral("alice"), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));
  s.settle();
  CHECK(s.tokens.read(oldKey).status == platform::CredentialStore::Status::NotFound);
  CHECK(s.tokens.read(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1"))).secret ==
        QStringLiteral("tok-new"));
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
  s.settle();
  CHECK(s.tokens.read(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1")))
            .status == platform::CredentialStore::Status::NotFound);
  REQUIRE(s.servers.servers().size() == 1);
  CHECK(s.servers.servers().front().userId.isEmpty());

  s.signIn.forgetServer(QStringLiteral("srv1"));
  CHECK(s.servers.servers().isEmpty());
  CHECK_FALSE(s.servers.active());
}

TEST_CASE("Signing out sends the playing item's reports first, then revokes the token") {
  Setup s;
  s.signIn.connectToServer(s.server.url().toString());
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::Credentials; }));
  s.signIn.signIn(QStringLiteral("alice"), QStringLiteral("secret"));
  REQUIRE(waitFor([&] { return s.signIn.step() == SignIn::Step::SignedIn; }));

  // What the player does at sign-out (prepareExit): a Stopped report with
  // the token, answered after a while.
  bool reportAnswered = false;
  s.signIn.setBeforeSignOut([&](std::function<void()> done) {
    s.client.rawRequest(api::JellyfinClient::Method::Post, QStringLiteral("Sessions/Playing/Stopped"), "{}",
                        nullptr, [&reportAnswered, done](int, QByteArray) {
                          reportAnswered = true;
                          done();
                        });
  });
  const qsizetype before = s.server.requests().size();
  QSignalSpy signedOut(&s.signIn, &SignIn::signedOut);
  s.signIn.signOut();
  // The local session ends at once.
  CHECK_FALSE(s.client.isSignedIn());
  CHECK(signedOut.count() == 1);
  CHECK(s.signIn.step() == SignIn::Step::Credentials);

  REQUIRE(waitFor([&] { return s.server.requests().size() == before + 2; }));
  CHECK(reportAnswered);
  const HttpRequest& report = s.server.requests().at(before);
  const HttpRequest& logout = s.server.requests().at(before + 1);
  CHECK(report.path == QStringLiteral("/Sessions/Playing/Stopped"));
  CHECK(report.headers.value("authorization").contains("Token=\"tok-new\""));
  CHECK(logout.path == QStringLiteral("/Sessions/Logout"));
  CHECK(logout.headers.value("authorization").contains("Token=\"tok-new\""));
}

TEST_CASE("Saved servers are listed most recent first") {
  QTemporaryDir dir;
  ServerStore store(dir.filePath(QStringLiteral("s.ini")));
  account::SavedServer a{QStringLiteral("a"),
                         QStringLiteral("A"),
                         QUrl(QStringLiteral("http://a")),
                         {},
                         {},
                         QDateTime::currentDateTimeUtc().addDays(-1)};
  account::SavedServer b{QStringLiteral("b"),
                         QStringLiteral("B"),
                         QUrl(QStringLiteral("http://b")),
                         {},
                         {},
                         // An hour ago: setActive() below stamps "now", which
                         // could fall in the same millisecond as b's.
                         QDateTime::currentDateTimeUtc().addSecs(-3600)};
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
