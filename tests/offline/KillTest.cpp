#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "FakeFileServer.h"
#include "OfflineFixtures.h"

#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/Manifest.h>

#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>

using namespace jfd::offline;
using namespace jfd::test;

namespace {
  QString downloadPath(const QString& id) {
    return QStringLiteral("/Items/") + id + QStringLiteral("/Download");
  }

  // After a kill: every manifest parses, nothing is complete with a wrong
  // file, and every .part is a true prefix of its file.
  void checkFolder(const DownloadStore& store, const QHash<QString, QByteArray>& contents) {
    QDirIterator it(store.root(), {QStringLiteral("item.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      QFile file(it.next());
      REQUIRE(file.open(QIODevice::ReadOnly));
      const auto m = manifestFromJson(QJsonDocument::fromJson(file.readAll()).object());
      REQUIRE(m);
      const QByteArray& content = contents[m->itemId];
      if (m->state == ItemState::Complete) {
        QFile media(store.mediaPath(*m));
        REQUIRE(media.open(QIODevice::ReadOnly));
        CHECK(FakeFileServer::sha256(media.readAll()) == FakeFileServer::sha256(content));
      }
      QFile part(store.partPath(*m));
      if (part.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = part.readAll();
        CHECK(bytes.size() <= content.size());
        CHECK(bytes == content.left(bytes.size()));
      }
    }
  }
} // namespace

TEST_CASE("Killed 20 times at random moments: never complete with a partial file, and it resumes") {
  FakeFileServer server;
  // Slow enough that the kills land mid-transfer: about 3 MB/s.
  server.chunkBytes = 32 * 1024;
  server.chunkIntervalMs = 10;
  QHash<QString, QByteArray> contents;
  contents.insert(itemIdFor(40), patternBytes(24 * MiB, 40));
  contents.insert(itemIdFor(41), patternBytes(384 * 1024, 41));
  for (auto it = contents.cbegin(); it != contents.cend(); ++it)
    server.setFile(downloadPath(it.key()), {it.value()});

  QTemporaryDir dir;
  const QString root = dir.filePath(QStringLiteral("downloads"));
  DownloadStore store(root);
  const QStringList args{root, server.url().toString(),
                         QString::number(contents[itemIdFor(40)].size()),
                         QString::number(contents[itemIdFor(41)].size())};

  const quint32 seed = QRandomGenerator::global()->generate();
  MESSAGE("kill test seed " << seed);
  QRandomGenerator random(seed);
  int kills = 0;
  for (int run = 0; run < 20; ++run) {
    QProcess child;
    child.start(QStringLiteral(JFD_OFFLINE_KILLEE), args);
    REQUIRE(child.waitForStarted());
    const int lifetime = static_cast<int>(random.bounded(40u, 400u));
    waitFor([&] { return child.state() == QProcess::NotRunning; }, lifetime);
    if (child.state() != QProcess::NotRunning) {
      // SIGKILL on Linux and macOS, TerminateProcess on Windows.
      child.kill();
      ++kills;
    }
    REQUIRE(child.waitForFinished(5000));
    checkFolder(store, contents);
  }
  MESSAGE("killed " << kills << " of 20 runs");
  CHECK(kills >= 10);

  // A last run, left alone, finishes both.
  QProcess child;
  child.start(QStringLiteral(JFD_OFFLINE_KILLEE), args);
  REQUIRE(child.waitForStarted());
  REQUIRE(waitFor([&] { return child.state() == QProcess::NotRunning; }, 60000));
  CHECK(child.exitStatus() == QProcess::NormalExit);
  CHECK(child.exitCode() == 0);
  checkFolder(store, contents);
  for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
    const auto m = store.readManifest(ServerId, UserId, it.key());
    REQUIRE(m);
    CHECK(m->state == ItemState::Complete);
  }
  // Runs after the first took up the .part with a range.
  bool resumed = false;
  for (const auto& r : server.requests())
    resumed = resumed || (r.status == 206 && !r.range.startsWith("bytes=0-"));
  CHECK(resumed);
  CHECK(filesContaining(root, {Token, "api_key", "apikey"}).isEmpty());
}
