#pragma once

#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/offline/ViewJournal.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

namespace jfd::api {
  class JellyfinClient;
}

// What the server and the downloads tell each other once the server can be
// reached again (the session socket is Ready), and every 30 minutes while
// it can:
//
// 1. The journal goes out. Per item: its playbacks (the last report of
//    each) count by Jellyfin's default rules (classifyView). If the
//    server's LastPlayedDate is later than the latest of them, the server
//    wins (the item was watched on another device since) and the journal
//    drops them. Otherwise the target state is computed once, written to
//    the journal, and sent with POST UserItems/{id}/UserData:
//    PlaybackPositionTicks, LastPlayedDate (the event's server time) and,
//    when a playback finished, Played and PlayCount = the server's count
//    plus the finished playbacks. Sent again after a lost answer, the same
//    values change nothing. Entries go only after a 2xx.
// 2. The manifests of the user's downloads take the server's user data
//    (watched marks and positions set on other devices show offline), and
//    learn whether the item is gone from the server or its file changed.
namespace jfd::offline {

  class OfflineSync : public QObject {
    Q_OBJECT

  public:
    OfflineSync(api::JellyfinClient& client, ViewJournal& journal, DownloadStore& store,
                OfflineCatalog& catalog, QObject* parent = nullptr);

    void setUser(const QString& serverId, const QString& userId);

    // Journal first, then the manifests. A run already going finishes
    // first; the request is remembered and runs after it.
    void syncNow();
    bool running() const { return m_running; }

    // Items per Items?ids= request.
    void setBatchSize(int size) { m_batchSize = size; }

  Q_SIGNALS:
    // `sent` items were updated on the server, `serverWon` were left as the
    // server has them.
    void journalSynced(int sent, int serverWon);
    void manifestsRefreshed(int changed);
    void finished();

  private:
    struct Plan {
      QString itemId;
      QStringList plays;
      std::optional<ViewUpdate> update;
    };

    void start();
    void fetchItems(QStringList ids, QHash<QString, QJsonObject> found,
                    std::function<void(bool ok, QHash<QString, QJsonObject>)> done);
    void sendJournal(const QHash<QString, QJsonObject>& items);
    void sendNext();
    void refreshManifests();
    void applyServerData(const QString& itemId, const QJsonObject& userData);
    void done();

    api::JellyfinClient& m_client;
    ViewJournal& m_journal;
    DownloadStore& m_store;
    OfflineCatalog& m_catalog;
    QString m_serverId;
    QString m_userId;
    bool m_running = false;
    bool m_again = false;
    int m_batchSize = 100;
    std::uint64_t m_generation = 0;

    QList<ViewUpdate> m_toSend;
    int m_sent = 0;
    int m_serverWon = 0;
  };

} // namespace jfd::offline
