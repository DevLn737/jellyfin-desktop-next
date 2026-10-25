#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/integrations/UpdateChecker.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>

using namespace jfd::integrations;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;

TEST_CASE("Versions compare by semantic version precedence") {
  CHECK(isNewerVersion(QStringLiteral("v0.2.0"), QStringLiteral("0.1.9")));
  CHECK(isNewerVersion(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")));
  CHECK(isNewerVersion(QStringLiteral("0.1.10"), QStringLiteral("0.1.9")));
  CHECK(isNewerVersion(QStringLiteral("0.2.0"), QStringLiteral("0.2.0-rc.1")));
  CHECK(isNewerVersion(QStringLiteral("0.2.0-rc.2"), QStringLiteral("0.2.0-rc.1")));
  CHECK(isNewerVersion(QStringLiteral("0.2.0-rc.10"), QStringLiteral("0.2.0-rc.9")));
  CHECK(isNewerVersion(QStringLiteral("0.2.0-rc"), QStringLiteral("0.2.0-beta")));
  CHECK_FALSE(isNewerVersion(QStringLiteral("0.2.0"), QStringLiteral("0.2.0")));
  CHECK_FALSE(isNewerVersion(QStringLiteral("0.2.0+build.5"), QStringLiteral("0.2.0")));
  CHECK_FALSE(isNewerVersion(QStringLiteral("0.1.0"), QStringLiteral("0.2.0")));
  CHECK_FALSE(isNewerVersion(QStringLiteral("0.2.0-rc.1"), QStringLiteral("0.2.0")));
  CHECK_FALSE(isNewerVersion(QStringLiteral("nightly"), QStringLiteral("0.2.0")));
  CHECK_FALSE(isNewerVersion(QStringLiteral("1.0.0"), QStringLiteral("dev")));
}

namespace {
  struct Result {
    QString version;
    QUrl page;
    bool upToDate = false;
    QString failure;
    bool done() const { return !version.isEmpty() || upToDate || !failure.isEmpty(); }
  };

  Result runCheck(FakeServer& server, const QString& current, UpdateChannel channel = UpdateChannel::Beta) {
    QNetworkAccessManager network;
    UpdateChecker checker(&network, current, channel, server.url(QStringLiteral("/repos/o/r/releases")));
    Result result;
    QObject::connect(&checker, &UpdateChecker::updateAvailable, [&](const QString& v, const QUrl& page) {
      result.version = v;
      result.page = page;
    });
    QObject::connect(&checker, &UpdateChecker::upToDate, [&] { result.upToDate = true; });
    QObject::connect(&checker, &UpdateChecker::checkFailed, [&](const QString& reason) { result.failure = reason; });
    checker.check();
    REQUIRE(waitFor([&] { return result.done(); }));
    if (!result.version.isEmpty()) {
      REQUIRE(checker.available());
      CHECK(checker.available()->version == result.version);
    }
    return result;
  }

  QString release(const QString& tag, bool prerelease = false, bool draft = false,
                  const QString& page = QString()) {
    const QString url = page.isEmpty() ? QStringLiteral("https://github.com/o/r/releases/tag/%1").arg(tag) : page;
    return QStringLiteral(R"({"tag_name":"%1","html_url":"%2","draft":%3,"prerelease":%4,"assets":[)"
                          R"({"name":"app-%1.AppImage","browser_download_url":"https://github.com/o/r/releases/download/%1/app.AppImage","size":42}]})")
        .arg(tag, url, draft ? QStringLiteral("true") : QStringLiteral("false"),
             prerelease ? QStringLiteral("true") : QStringLiteral("false"));
  }

  HttpResponse releases(const QStringList& items) {
    return jfd::test::json((QLatin1Char('[') + items.join(QLatin1Char(',')) + QLatin1Char(']')).toUtf8().constData());
  }

  QJsonArray parse(const QStringList& items) {
    return QJsonDocument::fromJson((QLatin1Char('[') + items.join(QLatin1Char(',')) + QLatin1Char(']')).toUtf8())
        .array();
  }
} // namespace

TEST_CASE("A newer release is offered with its page, without downloading anything") {
  FakeServer server;
  QList<HttpRequest> requests;
  server.setHandler([&](const HttpRequest& request) {
    requests.append(request);
    return releases({release(QStringLiteral("v0.2.0"))});
  });
  const Result result = runCheck(server, QStringLiteral("0.1.0"));
  CHECK(result.version == QStringLiteral("0.2.0"));
  CHECK(result.page == QUrl(QStringLiteral("https://github.com/o/r/releases/tag/v0.2.0")));
  REQUIRE(requests.size() == 1);
  CHECK(requests[0].method == QByteArray("GET"));
  CHECK(requests[0].path == QStringLiteral("/repos/o/r/releases"));
}

TEST_CASE("The same or an older release is up to date") {
  FakeServer server;
  server.setHandler([](const HttpRequest&) { return releases({release(QStringLiteral("v0.1.0"))}); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).upToDate);
  CHECK(runCheck(server, QStringLiteral("0.3.0")).upToDate);
}

TEST_CASE("Beta sees pre-releases of the client; stable does not") {
  const QStringList list = {release(QStringLiteral("v0.3.0-rc.1"), true), release(QStringLiteral("v0.2.0"))};
  auto beta = newestRelease(parse(list), UpdateChannel::Beta);
  REQUIRE(beta);
  CHECK(beta->version == QStringLiteral("0.3.0-rc.1"));
  CHECK(beta->prerelease);
  REQUIRE(beta->assets.size() == 1);
  CHECK(beta->assets[0].size == 42);
  CHECK(beta->assets[0].url.path().endsWith(QStringLiteral("/v0.3.0-rc.1/app.AppImage")));

  auto stable = newestRelease(parse(list), UpdateChannel::Stable);
  REQUIRE(stable);
  CHECK(stable->version == QStringLiteral("0.2.0"));

  // A release supersedes its pre-releases on the beta channel too.
  beta = newestRelease(parse({release(QStringLiteral("v0.3.0-rc.2"), true), release(QStringLiteral("v0.3.0"))}),
                       UpdateChannel::Beta);
  REQUIRE(beta);
  CHECK(beta->version == QStringLiteral("0.3.0"));

  FakeServer server;
  server.setHandler([&](const HttpRequest&) { return releases(list); });
  CHECK(runCheck(server, QStringLiteral("0.2.0"), UpdateChannel::Beta).version == QStringLiteral("0.3.0-rc.1"));
  CHECK(runCheck(server, QStringLiteral("0.2.0"), UpdateChannel::Stable).upToDate);

  CHECK(updateChannel(QStringLiteral("stable")) == UpdateChannel::Stable);
  CHECK(updateChannel(QStringLiteral("beta")) == UpdateChannel::Beta);
  CHECK(updateChannel(QString()) == UpdateChannel::Beta);
}

TEST_CASE("Other releases of the repository, drafts and foreign links are never offered") {
  // The SyncPlay test server's payloads are a release of the same repository.
  const QStringList others = {
      release(QStringLiteral("syncplay-test-server"), false),
      release(QStringLiteral("syncplay-test-server"), true),
      release(QStringLiteral("v9.0.0"), false, true),
      release(QStringLiteral("v8.0.0"), false, false, QStringLiteral("https://example.org/download")),
      release(QStringLiteral("8.0.0")),
      release(QStringLiteral("v8.0")),
  };
  CHECK_FALSE(newestRelease(parse(others), UpdateChannel::Beta));
  CHECK_FALSE(newestRelease(parse(others), UpdateChannel::Stable));

  QStringList withClient = others;
  withClient.append(release(QStringLiteral("v0.2.0-beta.1"), true));
  const auto newest = newestRelease(parse(withClient), UpdateChannel::Beta);
  REQUIRE(newest);
  CHECK(newest->version == QStringLiteral("0.2.0-beta.1"));
}

TEST_CASE("Missing releases and unexpected answers are failures") {
  FakeServer server;
  server.setHandler([](const HttpRequest&) { return jfd::test::json(R"({"message":"Not Found"})", 404); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).failure == QStringLiteral("HTTP 404"));

  server.setHandler([](const HttpRequest&) { return releases({}); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).failure == QStringLiteral("no client release"));

  server.setHandler([](const HttpRequest&) { return releases({release(QStringLiteral("syncplay-test-server"))}); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).failure == QStringLiteral("no client release"));

  server.setHandler([](const HttpRequest&) { return jfd::test::json(R"({"tag_name":"v1.0.0"})"); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).failure == QStringLiteral("unexpected answer"));

  server.setHandler([](const HttpRequest&) { return jfd::test::json("not json"); });
  CHECK_FALSE(runCheck(server, QStringLiteral("0.1.0")).failure.isEmpty());
}
