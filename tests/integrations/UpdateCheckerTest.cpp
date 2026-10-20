#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/integrations/UpdateChecker.h>

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

  Result runCheck(FakeServer& server, const QString& current) {
    QNetworkAccessManager network;
    UpdateChecker checker(&network, current, server.url(QStringLiteral("/repos/o/r/releases/latest")));
    Result result;
    QObject::connect(&checker, &UpdateChecker::updateAvailable, [&](const QString& v, const QUrl& page) {
      result.version = v;
      result.page = page;
    });
    QObject::connect(&checker, &UpdateChecker::upToDate, [&] { result.upToDate = true; });
    QObject::connect(&checker, &UpdateChecker::checkFailed, [&](const QString& reason) { result.failure = reason; });
    checker.check();
    REQUIRE(waitFor([&] { return result.done(); }));
    return result;
  }

  HttpResponse release(const char* tag, const char* page = "https://github.com/o/r/releases/tag/v0.2.0",
                       bool prerelease = false) {
    return jfd::test::json(QStringLiteral(R"({"tag_name":"%1","html_url":"%2","draft":false,"prerelease":%3})")
                               .arg(QString::fromLatin1(tag), QString::fromLatin1(page),
                                    prerelease ? QStringLiteral("true") : QStringLiteral("false"))
                               .toUtf8()
                               .constData());
  }
} // namespace

TEST_CASE("A newer release is offered with its page, without downloading anything") {
  FakeServer server;
  QList<HttpRequest> requests;
  server.setHandler([&](const HttpRequest& request) {
    requests.append(request);
    return release("v0.2.0");
  });
  const Result result = runCheck(server, QStringLiteral("0.1.0"));
  CHECK(result.version == QStringLiteral("0.2.0"));
  CHECK(result.page == QUrl(QStringLiteral("https://github.com/o/r/releases/tag/v0.2.0")));
  REQUIRE(requests.size() == 1);
  CHECK(requests[0].method == QByteArray("GET"));
  CHECK(requests[0].path == QStringLiteral("/repos/o/r/releases/latest"));
}

TEST_CASE("The same or an older release is up to date") {
  FakeServer server;
  server.setHandler([](const HttpRequest&) { return release("v0.1.0"); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).upToDate);
  CHECK(runCheck(server, QStringLiteral("0.3.0")).upToDate);
}

TEST_CASE("Missing releases, pre-releases and foreign links are not offered") {
  FakeServer server;
  server.setHandler([](const HttpRequest&) { return jfd::test::json(R"({"message":"Not Found"})", 404); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).failure == QStringLiteral("HTTP 404"));

  server.setHandler([](const HttpRequest&) { return release("v0.2.0", "https://github.com/o/r/releases/tag/v0.2.0", true); });
  CHECK_FALSE(runCheck(server, QStringLiteral("0.1.0")).failure.isEmpty());

  server.setHandler([](const HttpRequest&) { return release("v0.2.0", "https://example.org/download"); });
  CHECK(runCheck(server, QStringLiteral("0.1.0")).failure == QStringLiteral("unexpected release page"));

  server.setHandler([](const HttpRequest&) { return jfd::test::json("not json"); });
  CHECK_FALSE(runCheck(server, QStringLiteral("0.1.0")).failure.isEmpty());
}
