#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/integrations/AppImageUpdater.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTemporaryDir>

using namespace jfd::integrations;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;

// AppImages and the watchdog (sh) are Linux only.
#ifdef Q_OS_LINUX

// A file and its signature made by minisign 0.11 with a throwaway test key.
namespace {
  constexpr const char* testKey = "RWR805Y5+HyWC88PFRZwmQiH2B1OUjW2sVhRiL0YGJ/eKlWhWeCzjBeQ";
  const QByteArray newAppImage = QByteArrayLiteral("new AppImage 0.2.0\n");
  const QByteArray newSignature = QByteArrayLiteral(
      "untrusted comment: signature from minisign secret key\n"
      "RUR805Y5+HyWC6MGSZQepcngmC6RPLh6FtKN0pKRtWxYevh2/2R14E9FA7YCP/bBJheXAJMtp1J13vwDkgBQpAkFx9SHm73fUQg=\n"
      "trusted comment: file:jellyfin-desktop-next-0.2.0-x86_64.AppImage\n"
      "KD1mE8+jQ0EUjWaSb7cbn0QXWwnle48McmWAthZN9Q6K8Pkg6LMyO86EcsyDiibL2WQ5hNJd4QyvcYDxZdNWBg==\n");

  bool write(const QString& path, const QByteArray& data, bool executable = false) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) return false;
    file.close();
    return !executable || file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
  }

  QByteArray read(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }

  Release release(const FakeServer& server, qint64 size = newAppImage.size()) {
    const QString name = AppImageUpdater::assetName(QStringLiteral("0.2.0"));
    return {QStringLiteral("0.2.0"),
            QUrl(QStringLiteral("https://github.com/o/r/releases/tag/v0.2.0")),
            true,
            {{name, server.url(QStringLiteral("/a")), size},
             {name + QStringLiteral(".minisig"), server.url(QStringLiteral("/s")), newSignature.size()}}};
  }

  struct Outcome {
    QString ready;
    QString failure;
    bool done() const { return !ready.isEmpty() || !failure.isEmpty(); }
  };

  Outcome prepare(AppImageUpdater& updater, const Release& next) {
    Outcome outcome;
    QObject::connect(&updater, &AppImageUpdater::ready, [&](const QString& v) { outcome.ready = v; });
    QObject::connect(&updater, &AppImageUpdater::failed, [&](const QString& reason) { outcome.failure = reason; });
    updater.prepare(next);
    REQUIRE(waitFor([&] { return outcome.done(); }));
    return outcome;
  }

  // Runs the watchdog as apply() starts it, for an old version that has
  // already exited; returns when the watchdog (or what it execs) exits.
  void runWatchdog(const QString& app, const QString& marker, int tenths) {
    const int code = QProcess::execute(QStringLiteral("/bin/sh"),
                                       {QStringLiteral("-c"), AppImageUpdater::watchdogScript(), QStringLiteral("sh"),
                                        QStringLiteral("999999999"), app, app + QStringLiteral(".old"), marker,
                                        QString::number(tenths)});
    CHECK(code == 0);
  }

  // A pending update to 0.2.0, as apply() leaves it.
  void pending(const QString& marker) {
    REQUIRE(write(marker, QJsonDocument(QJsonObject{{QStringLiteral("version"), QStringLiteral("0.2.0")},
                                                     {QStringLiteral("route"), QStringLiteral("item/42")}})
                              .toJson()));
  }
} // namespace

TEST_CASE("A signed AppImage is downloaded next to the running one") {
  QTemporaryDir dir;
  const QString app = dir.filePath(QStringLiteral("Jellyfin.AppImage"));
  REQUIRE(write(app, "old", true));
  FakeServer server([](const HttpRequest& request) {
    return HttpResponse{200, request.path == QStringLiteral("/a") ? newAppImage : newSignature,
                        "application/octet-stream"};
  });
  QNetworkAccessManager network;
  AppImageUpdater updater(&network, app, dir.filePath(QStringLiteral("state")), parsePublicKey(testKey));
  REQUIRE(updater.supported());

  const Outcome outcome = prepare(updater, release(server));
  CHECK_MESSAGE(outcome.failure.isEmpty(), outcome.failure.toStdString());
  CHECK(outcome.ready == QStringLiteral("0.2.0"));
  CHECK(updater.readyVersion() == QStringLiteral("0.2.0"));
  CHECK(read(app + QStringLiteral(".new")) == newAppImage);
  CHECK(QFileInfo(app + QStringLiteral(".new")).isExecutable());
  CHECK(read(app) == "old");
}

TEST_CASE("A tampered download is rejected and removed") {
  QTemporaryDir dir;
  const QString app = dir.filePath(QStringLiteral("Jellyfin.AppImage"));
  REQUIRE(write(app, "old", true));
  QByteArray tampered = newAppImage;
  tampered[0] = 'N';
  FakeServer server([&](const HttpRequest& request) {
    return HttpResponse{200, request.path == QStringLiteral("/a") ? tampered : newSignature,
                        "application/octet-stream"};
  });
  QNetworkAccessManager network;
  AppImageUpdater updater(&network, app, dir.filePath(QStringLiteral("state")), parsePublicKey(testKey));
  const Outcome outcome = prepare(updater, release(server));
  CHECK(outcome.failure.startsWith(QStringLiteral("signature check failed")));
  CHECK(updater.readyVersion().isEmpty());
  CHECK_FALSE(QFile::exists(app + QStringLiteral(".new")));
  CHECK_FALSE(QFile::exists(app + QStringLiteral(".new.part")));
  CHECK_FALSE(updater.apply(QString()));
}

TEST_CASE("A short download or a release without a signature is not installed") {
  QTemporaryDir dir;
  const QString app = dir.filePath(QStringLiteral("Jellyfin.AppImage"));
  REQUIRE(write(app, "old", true));
  FakeServer server([](const HttpRequest& request) {
    return HttpResponse{200, request.path == QStringLiteral("/a") ? newAppImage : newSignature,
                        "application/octet-stream"};
  });
  QNetworkAccessManager network;
  AppImageUpdater updater(&network, app, dir.filePath(QStringLiteral("state")), parsePublicKey(testKey));
  CHECK(prepare(updater, release(server, 1000)).failure.startsWith(QStringLiteral("download incomplete")));

  Release unsigned_ = release(server);
  unsigned_.assets.removeLast();
  CHECK(prepare(updater, unsigned_).failure.contains(QStringLiteral("no signed AppImage")));
}

TEST_CASE("Without a release key or outside an AppImage nothing is installed") {
  QTemporaryDir dir;
  const QString app = dir.filePath(QStringLiteral("Jellyfin.AppImage"));
  REQUIRE(write(app, "old", true));
  QNetworkAccessManager network;
  CHECK_FALSE(AppImageUpdater(&network, app, dir.path(), std::nullopt).supported());
  CHECK_FALSE(AppImageUpdater(&network, QString(), dir.path(), parsePublicKey(testKey)).supported());
  CHECK(AppImageUpdater(&network, app, dir.path(), parsePublicKey(testKey)).supported());
}

TEST_CASE("The new version confirms its start and the old file is removed") {
  QTemporaryDir dir;
  const QString app = dir.filePath(QStringLiteral("Jellyfin.AppImage"));
  const QString marker = dir.filePath(QStringLiteral("update-pending.json"));
  pending(marker);
  // The new version confirms (as confirmStart does) and keeps running.
  REQUIRE(write(app, "#!/bin/sh\n: > '" + marker.toUtf8() + ".ok'\nsleep 1\n", true));
  REQUIRE(write(app + QStringLiteral(".old"), "#!/bin/sh\n: > '" + dir.filePath(QStringLiteral("old-ran")).toUtf8() + "'\n", true));
  runWatchdog(app, marker, 50);
  CHECK_FALSE(QFile::exists(app + QStringLiteral(".old")));
  CHECK_FALSE(QFile::exists(marker + QStringLiteral(".failed")));
  CHECK_FALSE(QFile::exists(dir.filePath(QStringLiteral("old-ran"))));
  CHECK(read(app).contains(".ok"));
}

TEST_CASE("A new version that exits or hangs without confirming is rolled back") {
  for (const QByteArray& body : {QByteArray("exit 1\n"), QByteArray("sleep 30\n")}) {
    CAPTURE(body.toStdString());
    QTemporaryDir dir;
    const QString app = dir.filePath(QStringLiteral("Jellyfin.AppImage"));
    const QString marker = dir.filePath(QStringLiteral("update-pending.json"));
    pending(marker);
    const QByteArray oldApp = "#!/bin/sh\n: > '" + dir.filePath(QStringLiteral("old-ran")).toUtf8() + "'\n";
    REQUIRE(write(app, "#!/bin/sh\n" + body, true));
    REQUIRE(write(app + QStringLiteral(".old"), oldApp, true));
    runWatchdog(app, marker, 20);
    CHECK(read(app) == oldApp);
    CHECK_FALSE(QFile::exists(app + QStringLiteral(".old")));
    CHECK(QFile::exists(dir.filePath(QStringLiteral("old-ran"))));

    // The old version, started again, says which update failed.
    QNetworkAccessManager network;
    AppImageUpdater updater(&network, app, dir.path(), parsePublicKey(testKey));
    const auto state = updater.confirmStart(QStringLiteral("0.1.0"));
    CHECK(state.kind == AppImageUpdater::StartState::Kind::RolledBack);
    CHECK(state.version == QStringLiteral("0.2.0"));
    CHECK_FALSE(QFile::exists(marker));
    CHECK(updater.confirmStart(QStringLiteral("0.1.0")).kind == AppImageUpdater::StartState::Kind::None);
  }
}

TEST_CASE("The updated version confirms and gets the screen to reopen") {
  QTemporaryDir dir;
  const QString marker = dir.filePath(QStringLiteral("update-pending.json"));
  pending(marker);
  QNetworkAccessManager network;
  AppImageUpdater updater(&network, dir.filePath(QStringLiteral("app")), dir.path(), parsePublicKey(testKey));
  const auto state = updater.confirmStart(QStringLiteral("0.2.0"));
  CHECK(state.kind == AppImageUpdater::StartState::Kind::Updated);
  CHECK(state.route == QStringLiteral("item/42"));
  CHECK(QFile::exists(marker + QStringLiteral(".ok")));
  CHECK_FALSE(QFile::exists(marker));
  CHECK(updater.confirmStart(QStringLiteral("0.2.0")).kind == AppImageUpdater::StartState::Kind::None);
}

TEST_CASE("Restart links are recognised by their scheme") {
  QNetworkAccessManager network;
  AppImageUpdater updater(&network, QString(), QString(), std::nullopt);
  int restarts = 0;
  QObject::connect(&updater, &AppImageUpdater::restartRequested, [&] { ++restarts; });
  updater.handleUrl(QUrl(QStringLiteral("jellyfin-desktop-update:restart")));
  updater.handleUrl(QUrl(QStringLiteral("https://github.com/")));
  CHECK(restarts == 1);
}

#endif
