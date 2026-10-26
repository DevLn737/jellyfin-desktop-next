#include "../support/DoctestQt.h"

#include "../support/FakeSeerr.h"

#include <jfd/platform/CredentialStore.h>
#include <jfd/seerr/RequestListModel.h>
#include <jfd/seerr/SeerrService.h>
#include <jfd/seerr/TitleDetailsModel.h>
#include <jfd/seerr/TitleListModel.h>

#include <QSignalSpy>

using namespace jfd::seerr;
using jfd::platform::MemoryCredentialStore;
using jfd::test::FakeSeerr;
using jfd::test::waitFor;

namespace {

  int signIn(SeerrService& service, const char* name, const char* password) {
    QSignalSpy done(&service, &SeerrService::signInFinished);
    service.signIn(QString::fromLatin1(name), QString::fromLatin1(password));
    REQUIRE(waitFor([&] { return done.count() == 1; }));
    return done.takeFirst().at(0).toInt();
  }

  int count(const FakeSeerr& seerr, const char* method, const QString& path) {
    int n = 0;
    for (const auto& r : seerr.server().requests()) {
      if (r.method == method && r.path == path) ++n;
    }
    return n;
  }

  void signedInService(SeerrService& service, FakeSeerr& seerr, const char* name = "alice") {
    service.setAddress(seerr.url().toString());
    REQUIRE(signIn(service, name, name == std::string("alice") ? "pw-alice" : "pw-viewer") ==
            int(Outcome::Ok));
  }

} // namespace

TEST_CASE("Without an address Seerr is off") {
  MemoryCredentialStore store;
  SeerrService service(store);
  CHECK_FALSE(service.configured());
  CHECK_FALSE(service.signedIn());
  QSignalSpy done(&service, &SeerrService::signInFinished);
  service.signIn(QStringLiteral("alice"), QStringLiteral("pw-alice"));
  REQUIRE(done.count() == 1);
  CHECK(done.first().at(0).toInt() == int(Outcome::NoAddress));
  service.setAddress(QStringLiteral("not an address"));
  CHECK_FALSE(service.configured());
}

TEST_CASE("Signing in keeps the cookie, and a new run restores it") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  {
    SeerrService service(store);
    signedInService(service, seerr);
    CHECK(service.signedIn());
    CHECK(service.userName() == QStringLiteral("alice"));
    CHECK(service.mayRequest());
    CHECK(service.serverHost().startsWith(QStringLiteral("127.0.0.1:")));
  }
  SeerrService again(store);
  QSignalSpy changed(&again, &SeerrService::stateChanged);
  again.setAddress(seerr.url().toString());
  CHECK(again.signedIn());
  // The name arrives from the server.
  REQUIRE(waitFor([&] { return again.userName() == QStringLiteral("alice"); }));
}

TEST_CASE("A wrong password and a server that is not Seerr on Jellyfin") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  service.setAddress(seerr.url().toString());
  CHECK(signIn(service, "alice", "wrong") == int(Outcome::BadLogin));
  CHECK_FALSE(service.signedIn());

  seerr.mediaServerType = 1; // Plex
  const int before = count(seerr, "POST", QStringLiteral("/api/v1/auth/jellyfin"));
  CHECK(signIn(service, "alice", "pw-alice") == int(Outcome::NotJellyfin));
  CHECK(count(seerr, "POST", QStringLiteral("/api/v1/auth/jellyfin")) == before);
}

TEST_CASE("Signing out and a session that ran out") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  signedInService(service, seerr);
  service.signOut();
  CHECK_FALSE(service.signedIn());
  CHECK(service.userName().isEmpty());
  SeerrService other(store);
  other.setAddress(seerr.url().toString());
  CHECK_FALSE(other.signedIn());

  signedInService(service, seerr);
  seerr.sessions().clear();
  QSignalSpy check(&service, &SeerrService::checkFinished);
  service.check();
  REQUIRE(waitFor([&] { return check.count() == 1; }));
  CHECK(check.first().at(0).toInt() == int(Outcome::SignedOut));
  CHECK_FALSE(service.signedIn());
}

TEST_CASE("The connection check tells the outcomes apart") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  service.setAddress(seerr.url().toString());
  QSignalSpy check(&service, &SeerrService::checkFinished);
  service.check();
  REQUIRE(waitFor([&] { return check.count() == 1; }));
  CHECK(check.takeFirst().at(0).toInt() == int(Outcome::SignedOut));
  REQUIRE(signIn(service, "alice", "pw-alice") == int(Outcome::Ok));
  service.check();
  REQUIRE(waitFor([&] { return check.count() == 1; }));
  CHECK(check.first().at(0).toInt() == int(Outcome::Ok));
  CHECK(check.first().at(1).toString() == QStringLiteral("alice"));
}

TEST_CASE("Title lists page, search and drop duplicates") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  signedInService(service, seerr);

  TitleListModel popular(service, TitleListModel::Source::PopularMovies);
  popular.reload();
  REQUIRE(waitFor([&] { return popular.count() == 2; }));
  CHECK(popular.hasMore());
  CHECK(popular.data(popular.index(0), TitleListModel::KeyRole).toString() == QStringLiteral("movie:603"));
  CHECK(popular.data(popular.index(0), TitleListModel::SubtitleRole).toString() == QStringLiteral("2021"));
  CHECK(popular.data(popular.index(0), TitleListModel::CanRequestRole).toBool() == false);
  CHECK(popular.data(popular.index(1), TitleListModel::CanRequestRole).toBool());
  CHECK(popular.data(popular.index(0), TitleListModel::ImageUrlRole).toUrl().host() ==
        QStringLiteral("image.tmdb.org"));
  // The fake repeats the same page: nothing new is added.
  popular.loadMore();
  REQUIRE(waitFor([&] { return !popular.loading(); }));
  CHECK(popular.count() == 2);

  TitleListModel trending(service, TitleListModel::Source::Trending);
  trending.reload();
  REQUIRE(waitFor([&] { return trending.count() == 2; })); // the person is dropped

  TitleListModel search(service, TitleListModel::Source::Search);
  search.search(QString());
  CHECK(search.count() == 0);
  search.search(QStringLiteral("matrix"));
  REQUIRE(waitFor([&] { return search.count() == 1; }));
  CHECK(search.data(search.index(0), TitleListModel::NameRole).toString() == QStringLiteral("Found matrix"));
  search.search(QString());
  CHECK(search.count() == 0);
}

TEST_CASE("A signed-out list reports it and a lost session reaches the service") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  service.setAddress(seerr.url().toString());
  TitleListModel popular(service, TitleListModel::Source::PopularTv);
  popular.reload();
  CHECK(popular.error() == int(Outcome::SignedOut));

  signedInService(service, seerr);
  seerr.sessions().clear();
  popular.reload();
  REQUIRE(waitFor([&] { return !service.signedIn(); }));
  CHECK(popular.error() == int(Outcome::SignedOut));
}

TEST_CASE("A movie page shows its availability") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  signedInService(service, seerr);
  TitleDetailsModel page(service);
  CHECK_FALSE(page.load(QStringLiteral("person:1")));
  REQUIRE(page.load(QStringLiteral("movie:603")));
  REQUIRE(waitFor([&] { return page.loaded(); }));
  CHECK(page.name() == QStringLiteral("The Matrix"));
  CHECK(page.year() == QStringLiteral("1999"));
  CHECK(page.runtimeMinutes() == 136);
  CHECK(page.genres() == QStringList{QStringLiteral("Action")});
  CHECK(page.status() == int(MediaStatus::Available));
  CHECK_FALSE(page.canRequest());
}

TEST_CASE("Requesting seasons of a series, then cancelling the request") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  signedInService(service, seerr);
  TitleDetailsModel page(service);
  REQUIRE(page.load(QStringLiteral("tv:1399")));
  REQUIRE(waitFor([&] { return page.loaded(); }));
  CHECK(page.status() == int(MediaStatus::PartiallyAvailable));
  const QVariantList seasons = page.seasons();
  REQUIRE(seasons.size() == 2); // specials are left out
  CHECK_FALSE(seasons[0].toMap().value(QStringLiteral("requestable")).toBool());
  CHECK(seasons[1].toMap().value(QStringLiteral("requestable")).toBool());
  CHECK(page.canRequest());
  CHECK(page.myRequestId() == 31);

  QSignalSpy finished(&page, &TitleDetailsModel::actionFinished);
  page.request(false, {2});
  REQUIRE(waitFor([&] { return finished.count() == 1; }));
  CHECK(finished.first().at(0).toInt() == int(Outcome::Ok));
  const auto body = seerr.lastRequestBody();
  CHECK(body.value(QStringLiteral("mediaType")).toString() == QStringLiteral("tv"));
  CHECK(body.value(QStringLiteral("seasons")).toArray().first().toInt() == 2);

  RequestListModel mine(service);
  mine.reload();
  REQUIRE(waitFor([&] { return mine.count() == 1; }));
  REQUIRE(waitFor([&] {
    return mine.data(mine.index(0), RequestListModel::NameRole).toString() == QStringLiteral("Thrones");
  }));
  CHECK(mine.data(mine.index(0), RequestListModel::CanCancelRole).toBool());
  CHECK(mine.data(mine.index(0), RequestListModel::KeyRole).toString() == QStringLiteral("tv:1399"));
  QSignalSpy cancelled(&mine, &RequestListModel::cancelFinished);
  mine.cancel(0);
  REQUIRE(waitFor([&] { return cancelled.count() == 1; }));
  CHECK(cancelled.first().at(0).toInt() == int(Outcome::Ok));
  REQUIRE(waitFor([&] { return mine.count() == 0; }));
}

TEST_CASE("An account that may not request cannot") {
  FakeSeerr seerr;
  MemoryCredentialStore store;
  SeerrService service(store);
  signedInService(service, seerr, "viewer");
  CHECK_FALSE(service.mayRequest());
  TitleDetailsModel page(service);
  REQUIRE(page.load(QStringLiteral("tv:1399")));
  REQUIRE(waitFor([&] { return page.loaded(); }));
  CHECK_FALSE(page.canRequest());
  CHECK(page.requestsNotAllowed());
  CHECK(page.myRequestId() == 0);
}
