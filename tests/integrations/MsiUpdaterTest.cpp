#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"

#include <jfd/integrations/MsiUpdater.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>

using namespace jfd::integrations;
using jfd::test::FakeServer;
using jfd::test::HttpRequest;
using jfd::test::HttpResponse;
using jfd::test::waitFor;

// Everything but the process start runs on every platform.

// A file and its signature made by minisign 0.11 with a throwaway test key.
namespace {
  constexpr const char* testKey = "RWRrLMuGH6cumqwldRaRJzhfK+oDeMgMd6r0LKTeST4p273XcqTT65OJ";
  const QByteArray newMsi = QByteArrayLiteral("new installer 0.2.0\n");
  // Signed as "file:jellyfin-desktop-next-0.2.0-x64.msi".
  const QByteArray msiSignature = QByteArrayLiteral(
      "untrusted comment: signature from minisign secret key\n"
      "RURrLMuGH6cumqHxDLSpYiB9Fg1bfc9ZDFHidJtkED9jvlpee5WvXD5aYJnzXI4FtTfc4NCO+cYTIEvIuVqXMZ72ppyT9S6qQQ4=\n"
      "trusted comment: file:jellyfin-desktop-next-0.2.0-x64.msi\n"
      "43a2DvWezVONJ+cLn5AcHOdcdl2bxyZ5Z9TVmmkXugD9XAID13CD4o2MttKYfsC+mIjCA5a1CF6BBiVN/U/NAA==\n");
  // Signed as the AppImage of the same version: not valid for the MSI.
  const QByteArray otherSignature = QByteArrayLiteral(
      "untrusted comment: signature from minisign secret key\n"
      "RURrLMuGH6cumqHxDLSpYiB9Fg1bfc9ZDFHidJtkED9jvlpee5WvXD5aYJnzXI4FtTfc4NCO+cYTIEvIuVqXMZ72ppyT9S6qQQ4=\n"
      "trusted comment: file:jellyfin-desktop-next-0.2.0-x86_64.AppImage\n"
      "ySKBvEdmZoGUvhVKPtSbiUcP14cvMk2LUkS4woe8G1/rfQq4XjbxRIyjPcvNecQ0DRev1BjU/lUkwqpSJBQ6Aw==\n");

  bool write(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) return false;
    file.close();
    return true;
  }

  Release release(const FakeServer& server, const QByteArray& signature, qint64 size = newMsi.size()) {
    const QString name = MsiUpdater::assetName(QStringLiteral("0.2.0"));
    return {QStringLiteral("0.2.0"),
            QUrl(QStringLiteral("https://github.com/o/r/releases/tag/v0.2.0")),
            true,
            {{name, server.url(QStringLiteral("/a")), size},
             {name + QStringLiteral(".minisig"), server.url(QStringLiteral("/s")), signature.size()}}};
  }

  struct Outcome {
    QString ready;
    QString failure;
    bool done() const { return !ready.isEmpty() || !failure.isEmpty(); }
  };

  Outcome prepare(MsiUpdater& updater, const Release& next) {
    Outcome outcome;
    QObject::connect(&updater, &MsiUpdater::ready, [&](const QString& v) { outcome.ready = v; });
    QObject::connect(&updater, &MsiUpdater::failed, [&](const QString& reason) { outcome.failure = reason; });
    updater.prepare(next);
    REQUIRE(waitFor([&] { return outcome.done(); }));
    return outcome;
  }

  // The folder of an installed application: the program file in it.
  QString install(const QTemporaryDir& dir) {
    const QString folder = dir.filePath(QStringLiteral("Programs/App"));
    REQUIRE(QDir().mkpath(folder));
    REQUIRE(write(folder + QStringLiteral("/app.exe"), "old"));
    return folder;
  }

  void pending(const QString& marker) {
    REQUIRE(write(marker, QJsonDocument(QJsonObject{{QStringLiteral("version"), QStringLiteral("0.2.0")},
                                                     {QStringLiteral("route"), QStringLiteral("item/42")}})
                              .toJson()));
  }
} // namespace

TEST_CASE("The MSI of a release is named after its version") {
  CHECK(MsiUpdater::assetName(QStringLiteral("0.2.0")) == QStringLiteral("jellyfin-desktop-next-0.2.0-x64.msi"));
}

TEST_CASE("A signed MSI is downloaded into the state directory") {
  QTemporaryDir dir;
  const QString folder = install(dir);
  const QString state = dir.filePath(QStringLiteral("state"));
  FakeServer server([](const HttpRequest& request) {
    return HttpResponse{200, request.path == QStringLiteral("/a") ? newMsi : msiSignature, "application/octet-stream"};
  });
  QNetworkAccessManager network;
  MsiUpdater updater(&network, folder + QStringLiteral("/app.exe"), folder, state, parsePublicKey(testKey));
  REQUIRE(updater.supported());

  const Outcome outcome = prepare(updater, release(server, msiSignature));
  CHECK_MESSAGE(outcome.failure.isEmpty(), outcome.failure.toStdString());
  CHECK(outcome.ready == QStringLiteral("0.2.0"));
  CHECK(updater.readyVersion() == QStringLiteral("0.2.0"));
  QFile msi(state + QStringLiteral("/jellyfin-desktop-next-0.2.0-x64.msi"));
  REQUIRE(msi.open(QIODevice::ReadOnly));
  CHECK(msi.readAll() == newMsi);
  CHECK_FALSE(QFile::exists(msi.fileName() + QStringLiteral(".minisig")));
}

TEST_CASE("A tampered MSI or a signature of another file is rejected and removed") {
  for (const bool tamper : {true, false}) {
    CAPTURE(tamper);
    QTemporaryDir dir;
    const QString folder = install(dir);
    const QString state = dir.filePath(QStringLiteral("state"));
    QByteArray body = newMsi;
    if (tamper) body[0] = 'N';
    const QByteArray signature = tamper ? msiSignature : otherSignature;
    FakeServer server([&](const HttpRequest& request) {
      return HttpResponse{200, request.path == QStringLiteral("/a") ? body : signature, "application/octet-stream"};
    });
    QNetworkAccessManager network;
    MsiUpdater updater(&network, folder + QStringLiteral("/app.exe"), folder, state, parsePublicKey(testKey));
    const Outcome outcome = prepare(updater, release(server, signature));
    CHECK(outcome.failure.startsWith(QStringLiteral("signature check failed")));
    CHECK(updater.readyVersion().isEmpty());
    CHECK(QDir(state).entryList(QDir::Files).isEmpty());
    CHECK_FALSE(updater.apply(QString()));
  }
}

TEST_CASE("A short download or a release without a signature is not installed") {
  QTemporaryDir dir;
  const QString folder = install(dir);
  FakeServer server([](const HttpRequest& request) {
    return HttpResponse{200, request.path == QStringLiteral("/a") ? newMsi : msiSignature, "application/octet-stream"};
  });
  QNetworkAccessManager network;
  MsiUpdater updater(&network, folder + QStringLiteral("/app.exe"), folder, dir.filePath(QStringLiteral("state")),
                     parsePublicKey(testKey));
  CHECK(prepare(updater, release(server, msiSignature, 1000)).failure.startsWith(QStringLiteral("download incomplete")));

  Release unsigned_ = release(server, msiSignature);
  unsigned_.assets.removeLast();
  CHECK(prepare(updater, unsigned_).failure.contains(QStringLiteral("no signed MSI")));
}

TEST_CASE("Only an installed application with a release key updates itself") {
  QTemporaryDir dir;
  const QString folder = install(dir);
  const QString exe = folder + QStringLiteral("/app.exe");
  const QString state = dir.filePath(QStringLiteral("state"));
  QNetworkAccessManager network;
  CHECK(MsiUpdater(&network, exe, folder, state, parsePublicKey(testKey)).supported());
  // Windows paths: other separators, trailing separator, other case.
  CHECK(MsiUpdater(&network, exe, QString(folder).toUpper() + QStringLiteral("/"), state, parsePublicKey(testKey))
            .supported());
  CHECK_FALSE(MsiUpdater(&network, exe, folder, state, std::nullopt).supported());
  CHECK_FALSE(MsiUpdater(&network, exe, QString(), state, parsePublicKey(testKey)).supported());
  CHECK_FALSE(MsiUpdater(&network, QString(), folder, state, parsePublicKey(testKey)).supported());
  // Run from somewhere else, like a build folder.
  CHECK_FALSE(MsiUpdater(&network, exe, dir.filePath(QStringLiteral("Programs/Other")), state, parsePublicKey(testKey))
                  .supported());
  CHECK_FALSE(MsiUpdater(&network, folder + QStringLiteral("/missing.exe"), folder, state, parsePublicKey(testKey))
                  .supported());
}

TEST_CASE("The update script waits, installs per user, and starts the program either way") {
  const QString script = MsiUpdater::updateScript();
  CHECK(script.contains(QStringLiteral("Wait-Process -Id $ProcessId")));
  CHECK(script.contains(QStringLiteral("'msiexec.exe'")));
  CHECK(script.contains(QStringLiteral("'/qb', '/norestart', '/l*v'")));
  CHECK(script.contains(QStringLiteral("New-Item -ItemType File -Path \"$Marker.failed\"")));
  // The program is started after the failure marker is decided, not only on success.
  CHECK(script.indexOf(QStringLiteral("Start-Process -FilePath $Exe")) > script.indexOf(QStringLiteral(".failed")));
  CHECK_FALSE(script.contains(QStringLiteral("RunAs")));

  const QStringList arguments = MsiUpdater::scriptArguments(QStringLiteral("C:/s/apply-update.ps1"), 4242,
                                                           QStringLiteral("C:/s/a b.msi"), QStringLiteral("C:/p/app.exe"),
                                                           QStringLiteral("C:/s/log.txt"), QStringLiteral("C:/s/marker"));
  CHECK(arguments.contains(QStringLiteral("Bypass")));
  const qsizetype file = arguments.indexOf(QStringLiteral("-File"));
  REQUIRE(file >= 0);
  CHECK(arguments.at(file + 1) == QStringLiteral("C:/s/apply-update.ps1"));
  CHECK(arguments.at(arguments.indexOf(QStringLiteral("-ProcessId")) + 1) == QStringLiteral("4242"));
  CHECK(arguments.at(arguments.indexOf(QStringLiteral("-Msi")) + 1) == QStringLiteral("C:/s/a b.msi"));
}

TEST_CASE("Apply does nothing before a verified MSI is ready") {
  QTemporaryDir dir;
  const QString folder = install(dir);
  QNetworkAccessManager network;
  MsiUpdater updater(&network, folder + QStringLiteral("/app.exe"), folder, dir.path(), parsePublicKey(testKey));
  CHECK_FALSE(updater.apply(QStringLiteral("item/42")));
  CHECK_FALSE(QFile::exists(dir.filePath(QStringLiteral("update-pending.json"))));
}

TEST_CASE("The updated version confirms and gets the screen to reopen") {
  QTemporaryDir dir;
  const QString marker = dir.filePath(QStringLiteral("update-pending.json"));
  pending(marker);
  QNetworkAccessManager network;
  MsiUpdater updater(&network, QString(), QString(), dir.path(), parsePublicKey(testKey));
  const auto state = updater.confirmStart(QStringLiteral("0.2.0"));
  CHECK(state.kind == MsiUpdater::StartState::Kind::Updated);
  CHECK(state.route == QStringLiteral("item/42"));
  CHECK_FALSE(QFile::exists(marker));
  CHECK(updater.confirmStart(QStringLiteral("0.2.0")).kind == MsiUpdater::StartState::Kind::None);
}

TEST_CASE("A failed install leaves the old version running and says which one failed") {
  QTemporaryDir dir;
  const QString marker = dir.filePath(QStringLiteral("update-pending.json"));
  pending(marker);
  REQUIRE(write(marker + QStringLiteral(".failed"), ""));
  QNetworkAccessManager network;
  MsiUpdater updater(&network, QString(), QString(), dir.path(), parsePublicKey(testKey));
  const auto state = updater.confirmStart(QStringLiteral("0.1.0"));
  CHECK(state.kind == MsiUpdater::StartState::Kind::RolledBack);
  CHECK(state.version == QStringLiteral("0.2.0"));
  CHECK_FALSE(QFile::exists(marker));
  CHECK_FALSE(QFile::exists(marker + QStringLiteral(".failed")));
  CHECK(updater.confirmStart(QStringLiteral("0.1.0")).kind == MsiUpdater::StartState::Kind::None);
}

TEST_CASE("A marker of another version is dropped without a report") {
  QTemporaryDir dir;
  const QString marker = dir.filePath(QStringLiteral("update-pending.json"));
  pending(marker);
  QNetworkAccessManager network;
  MsiUpdater updater(&network, QString(), QString(), dir.path(), parsePublicKey(testKey));
  CHECK(updater.confirmStart(QStringLiteral("0.1.0")).kind == MsiUpdater::StartState::Kind::None);
  CHECK_FALSE(QFile::exists(marker));
}

TEST_CASE("Restart links are recognised by their scheme") {
  QNetworkAccessManager network;
  MsiUpdater updater(&network, QString(), QString(), QString(), std::nullopt);
  int restarts = 0;
  QObject::connect(&updater, &MsiUpdater::restartRequested, [&] { ++restarts; });
  updater.handleUrl(QUrl(QStringLiteral("jellyfin-desktop-update:restart")));
  updater.handleUrl(QUrl(QStringLiteral("https://github.com/")));
  CHECK(restarts == 1);
}
