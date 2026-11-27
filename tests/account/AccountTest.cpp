#include "library/LibraryTestSupport.h"

#include <jfd/account/Account.h>
#include <jfd/account/UserScope.h>
#include <jfd/platform/CredentialStore.h>

#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

using namespace jfd;
using jfd::account::Account;
using jfd::account::SignIn;
using jfd::account::UserScope;
using namespace jfd::test;

namespace {
  // Records the hooks in the order they ran.
  struct Journal {
    QStringList lines;
    UserScope::Hook hook(const QString& line) {
      return [this, line] { lines.append(line); };
    }
  };

  void addPart(UserScope& scope, Journal& journal, const QString& name, int order, QObject* context = nullptr) {
    scope.add(name, order, context, journal.hook(name + QStringLiteral("+")), journal.hook(name + QStringLiteral("-")));
  }

  HttpResponse serve(const HttpRequest& r, bool& rejectToken) {
    const QString p = r.path;
    if (p == QLatin1String("/System/Info/Public"))
      return ok(R"({"Id": "srv1", "ServerName": "Home", "Version": "12.2.0", "StartupWizardCompleted": true})");
    if (p == QLatin1String("/QuickConnect/Enabled")) return ok("false");
    if (p == QLatin1String("/Users/AuthenticateByName"))
      return ok(R"({"AccessToken": "tok", "ServerId": "srv1", "User": {"Id": "u1", "Name": "alice", "ServerId": "srv1"}})");
    if (p == QLatin1String("/Users/Me"))
      return rejectToken ? HttpResponse{401, {}, {}, 0} : ok(R"({"Id": "u1", "Name": "alice", "ServerId": "srv1"})");
    if (p == QLatin1String("/Items"))
      return rejectToken ? HttpResponse{401, {}, {}, 0} : ok(page(""));
    if (p == QLatin1String("/Sessions/Logout")) return HttpResponse{204, {}, {}, 0};
    return {404, {}, {}, 0};
  }

  struct Setup {
    bool rejectToken = false;
    FakeServer server{[this](const HttpRequest& r) { return serve(r, rejectToken); }};
    QTemporaryDir dir;
    std::unique_ptr<Account> account;
    Journal journal;

    Setup() {
      Account::Config config;
      config.serversFile = dir.filePath(QStringLiteral("servers.ini"));
      config.tokens = std::make_unique<platform::MemoryCredentialStore>();
      account = std::make_unique<Account>(std::move(config));
    }

    void signIn() {
      SignIn& flow = account->signIn();
      flow.connectToServer(server.url().toString());
      REQUIRE(waitFor([&] { return flow.step() == SignIn::Step::Credentials; }));
      flow.signIn(QStringLiteral("alice"), QStringLiteral("secret"));
      REQUIRE(waitFor([&] { return flow.step() == SignIn::Step::SignedIn; }));
    }
  };
} // namespace

TEST_CASE("The user scope begins its parts from the lowest order and ends them from the highest") {
  UserScope scope;
  Journal journal;
  addPart(scope, journal, QStringLiteral("music"), UserScope::Music);
  addPart(scope, journal, QStringLiteral("library"), UserScope::Library);
  addPart(scope, journal, QStringLiteral("player"), UserScope::Player);
  addPart(scope, journal, QStringLiteral("library2"), UserScope::Library);
  CHECK(scope.names() == QStringList{QStringLiteral("library"), QStringLiteral("library2"), QStringLiteral("music"),
                                     QStringLiteral("player")});
  CHECK_FALSE(scope.active());

  scope.begin();
  CHECK(scope.active());
  CHECK(journal.lines == QStringList{QStringLiteral("library+"), QStringLiteral("library2+"), QStringLiteral("music+"),
                                     QStringLiteral("player+")});
  // Only once.
  scope.begin();
  CHECK(journal.lines.size() == 4);

  journal.lines.clear();
  scope.end();
  CHECK_FALSE(scope.active());
  CHECK(journal.lines == QStringList{QStringLiteral("player-"), QStringLiteral("music-"), QStringLiteral("library2-"),
                                     QStringLiteral("library-")});
  scope.end();
  CHECK(journal.lines.size() == 4);
}

TEST_CASE("A part added during a session begins at once; one whose context is gone is skipped") {
  UserScope scope;
  Journal journal;
  auto context = std::make_unique<QObject>();
  addPart(scope, journal, QStringLiteral("group"), UserScope::Group, context.get());
  addPart(scope, journal, QStringLiteral("session"), UserScope::Session);
  scope.begin();
  addPart(scope, journal, QStringLiteral("late"), UserScope::Downloads);
  CHECK(journal.lines == QStringList{QStringLiteral("session+"), QStringLiteral("group+"), QStringLiteral("late+")});

  // Empty hooks are allowed.
  scope.add(QStringLiteral("quiet"), UserScope::Player, nullptr, {}, {});

  journal.lines.clear();
  context.reset();
  scope.end();
  CHECK(journal.lines == QStringList{QStringLiteral("late-"), QStringLiteral("session-")});
}

TEST_CASE("Signing in begins the user scope and starts the socket; signing out ends both, in order") {
  Setup s;
  QSignalSpy began(s.account.get(), &Account::sessionBegan);
  QSignalSpy ended(s.account.get(), &Account::sessionEnded);
  // The screens follow the signals: by then every part has run.
  QObject::connect(s.account.get(), &Account::sessionBegan, [&] { s.journal.lines.append(QStringLiteral("began")); });
  QObject::connect(s.account.get(), &Account::sessionEnded, [&] { s.journal.lines.append(QStringLiteral("ended")); });
  addPart(s.account->scope(), s.journal, QStringLiteral("library"), UserScope::Library);
  addPart(s.account->scope(), s.journal, QStringLiteral("player"), UserScope::Player);
  CHECK_FALSE(s.account->signedIn());

  s.signIn();
  CHECK(s.account->signedIn());
  CHECK(began.count() == 1);
  CHECK(s.account->socket().state() != api::SessionSocket::State::Stopped);
  CHECK(s.journal.lines ==
        QStringList{QStringLiteral("library+"), QStringLiteral("player+"), QStringLiteral("began")});

  s.journal.lines.clear();
  s.account->signIn().signOut();
  CHECK_FALSE(s.account->signedIn());
  CHECK(ended.count() == 1);
  CHECK(s.account->socket().state() == api::SessionSocket::State::Stopped);
  CHECK(s.journal.lines ==
        QStringList{QStringLiteral("player-"), QStringLiteral("library-"), QStringLiteral("ended")});
}

TEST_CASE("A session the server ends also ends the user scope") {
  Setup s;
  addPart(s.account->scope(), s.journal, QStringLiteral("music"), UserScope::Music);
  s.signIn();
  REQUIRE(s.account->signedIn());
  s.journal.lines.clear();

  s.rejectToken = true;
  // Any request refused as signed out (checked once more with Users/Me).
  s.account->client().items(api::ItemQuery{}, s.account.get(), [](api::Result<api::ItemPage>) {});
  REQUIRE(waitFor([&] { return !s.account->signedIn(); }));
  CHECK(s.journal.lines == QStringList{QStringLiteral("music-")});
  CHECK(s.account->signIn().step() == SignIn::Step::Credentials);
}

TEST_CASE("A part registered after the saved session was restored begins at once") {
  Setup s;
  s.signIn();
  // A second account on the same profile, as at the next start.
  Account::Config config;
  config.serversFile = s.dir.filePath(QStringLiteral("servers.ini"));
  auto tokens = std::make_unique<platform::MemoryCredentialStore>();
  tokens->write(platform::accessTokenKey(QStringLiteral("srv1"), QStringLiteral("u1")), QStringLiteral("tok"));
  config.tokens = std::move(tokens);
  Account next(std::move(config));
  // Waits for the store, as at start-up.
  REQUIRE(next.restore(std::chrono::seconds(5)));
  CHECK(next.signedIn());
  addPart(next.scope(), s.journal, QStringLiteral("session"), UserScope::Session);
  CHECK(s.journal.lines.contains(QStringLiteral("session+")));
}
