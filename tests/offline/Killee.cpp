// The process the kill test kills: a download queue on a folder, against
// the test's file server. Arguments: <download folder> <server URL>
// <size of item 40> <size of item 41>. Takes up what an earlier, killed run
// left, queues both items if they are not there yet, and exits 0 once both
// are complete (1 if one fails).
#include "OfflineFixtures.h"

#include <jfd/offline/DownloadQueue.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/OfflineCatalog.h>

#include <QCoreApplication>
#include <QTimer>

using namespace jfd::offline;
using namespace jfd::test;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  if (args.size() != 5) return 2;
  DownloadStore store(args[1]);
  OfflineCatalog catalog(store);
  catalog.load();
  auto guard = std::make_shared<StorageGuard>(0, 0);
  DownloadQueue queue(store, catalog, guard);
  queue.setRetryDelaysMs({10, 10, 10, 10, 10});
  queue.setAccess(ServerId, UserId, QUrl(args[2]), "MediaBrowser Token=\"" + Token + "\"");

  const QStringList ids{itemIdFor(40), itemIdFor(41)};
  for (int i = 0; i < 2; ++i) {
    if (catalog.find(ServerId, UserId, ids[i])) continue;
    const QJsonObject source = mediaSourceJson(ids[i], args[3 + i].toLongLong());
    queue.enqueueOriginal(itemJson(ids[i], source), source);
  }
  const auto check = [&]() {
    int complete = 0;
    for (const QString& id : ids) {
      const auto m = catalog.find(ServerId, UserId, id);
      if (m && m->state == ItemState::Complete) ++complete;
      if (m && m->state == ItemState::Failed) app.exit(1);
    }
    if (complete == 2) app.exit(0);
  };
  QObject::connect(&queue, &DownloadQueue::itemChanged, &app, check);
  QTimer::singleShot(0, &app, check);
  return app.exec();
}
