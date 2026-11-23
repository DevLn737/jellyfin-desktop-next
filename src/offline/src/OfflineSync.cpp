#include <jfd/offline/OfflineSync.h>

#include <jfd/api/Downloads.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/ServerTime.h>

#include <QDateTime>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>

namespace jfd::offline {

  namespace {
    Q_LOGGING_CATEGORY(lcSync, "jfd.offline.sync")

    constexpr std::int64_t TicksPerMs = 10'000;

    std::optional<std::int64_t> serverMs(const QJsonValue& value) {
      const auto instant = api::parseServerInstant(value.toString());
      if (!instant) return std::nullopt;
      return instant->sinceOrigin().count() / TicksPerMs;
    }

    QString serverDate(std::int64_t ms) {
      return api::formatServerInstant(core::ServerInstant::fromSinceOrigin(core::WallTicks(ms * TicksPerMs)));
    }

    std::int64_t integer(const QJsonValue& value) {
      const double d = value.toDouble();
      return d > 0 && d < 9007199254740992.0 ? static_cast<std::int64_t>(d) : 0;
    }
  } // namespace

  OfflineSync::OfflineSync(api::JellyfinClient& client, ViewJournal& journal, DownloadStore& store,
                           OfflineCatalog& catalog, QObject* parent)
      : QObject(parent), m_client(client), m_journal(journal), m_store(store), m_catalog(catalog) {}

  void OfflineSync::setUser(const QString& serverId, const QString& userId) {
    if (serverId == m_serverId && userId == m_userId) return;
    m_serverId = serverId;
    m_userId = userId;
    // Answers for the previous user are dropped.
    ++m_generation;
    m_running = false;
    m_again = false;
    m_toSend.clear();
  }

  void OfflineSync::syncNow() {
    if (m_serverId.isEmpty() || m_userId.isEmpty()) return;
    if (m_running) {
      m_again = true;
      return;
    }
    start();
  }

  void OfflineSync::start() {
    m_running = true;
    m_sent = 0;
    m_serverWon = 0;
    QSet<QString> ids;
    for (const ViewEvent& e : m_journal.events(m_serverId, m_userId))
      ids.insert(e.itemId);
    for (const ViewUpdate& u : m_journal.updates(m_serverId, m_userId))
      ids.insert(u.itemId);
    if (ids.isEmpty()) {
      refreshManifests();
      return;
    }
    QStringList list = ids.values();
    list.sort();
    fetchItems(list, {}, [this, ids](bool ok, QHash<QString, QJsonObject> items) {
      if (!ok) return done();
      sendJournal(items, ids);
    });
  }

  void OfflineSync::fetchItems(QStringList ids, QHash<QString, QJsonObject> found,
                               std::function<void(bool, QHash<QString, QJsonObject>)> finish) {
    if (ids.isEmpty()) {
      finish(true, std::move(found));
      return;
    }
    const QStringList batch = ids.mid(0, m_batchSize);
    ids = ids.mid(m_batchSize);
    const std::uint64_t generation = m_generation;
    api::fetchItemsById(
        m_client, batch, this,
        [this, generation, ids = std::move(ids), found = std::move(found),
         finish = std::move(finish)](api::Result<QJsonArray> result) mutable {
          if (generation != m_generation) return;
          if (!result) {
            qCInfo(lcSync) << "items not fetched:" << result.error().message;
            finish(false, {});
            return;
          }
          for (const auto& value : *result) {
            const QJsonObject item = value.toObject();
            found.insert(item.value(QLatin1String("Id")).toString(), item);
          }
          fetchItems(std::move(ids), std::move(found), std::move(finish));
        });
  }

  void OfflineSync::sendJournal(const QHash<QString, QJsonObject>& items, const QSet<QString>& requested) {
    const QList<ViewEvent> events = m_journal.events(m_serverId, m_userId);
    QStringList itemIds;
    const auto add = [this, &itemIds, &requested](const QString& itemId) {
      if (itemIds.contains(itemId)) return;
      // Journaled after the fetch went out (a report that failed while it
      // ran): not looked up, so not known to be gone. The next run takes it.
      if (!requested.contains(itemId)) {
        m_again = true;
        return;
      }
      itemIds.append(itemId);
    };
    for (const ViewEvent& e : events)
      add(e.itemId);
    for (const ViewUpdate& u : m_journal.updates(m_serverId, m_userId))
      add(u.itemId);

    m_toSend.clear();
    for (const QString& itemId : std::as_const(itemIds)) {
      // The last report of each playback, in the order they were played.
      QStringList plays;
      QHash<QString, ViewEvent> last;
      for (const ViewEvent& e : events) {
        if (e.itemId != itemId) continue;
        if (!last.contains(e.playId)) plays.append(e.playId);
        last.insert(e.playId, e);
      }
      std::stable_sort(plays.begin(), plays.end(), [&last](const QString& a, const QString& b) {
        return last.value(a).atMs < last.value(b).atMs;
      });
      const std::optional<ViewUpdate> pending = m_journal.update(m_serverId, m_userId, itemId);

      const auto item = items.constFind(itemId);
      if (item == items.constEnd()) {
        // Gone from the server, or no longer visible to the user.
        qCInfo(lcSync) << "journal: item" << itemId << "not on the server; dropped";
        m_journal.removeItem(m_serverId, m_userId, itemId, plays);
        continue;
      }
      const QJsonObject userData = item->value(QLatin1String("UserData")).toObject();
      std::int64_t latest = pending ? pending->atMs : 0;
      for (const QString& play : std::as_const(plays))
        latest = std::max(latest, last.value(play).atMs);
      const auto serverLast = serverMs(userData.value(QLatin1String("LastPlayedDate")));
      if (serverLast && *serverLast > latest) {
        // Watched elsewhere since: the server's state stands.
        qCInfo(lcSync) << "journal: the server's state of" << itemId << "is newer; skipped";
        m_journal.removeItem(m_serverId, m_userId, itemId, plays);
        applyServerData(itemId, userData);
        ++m_serverWon;
        continue;
      }
      if (plays.isEmpty() && pending) {
        // Computed before, the answer was lost: the same values again.
        m_toSend.append(*pending);
        continue;
      }

      QJsonObject body = pending ? pending->body : QJsonObject();
      bool played = body.value(QLatin1String("Played")).toBool();
      int playCount = body.contains(QLatin1String("PlayCount"))
                          ? body.value(QLatin1String("PlayCount")).toInt()
                          : userData.value(QLatin1String("PlayCount")).toInt();
      std::optional<std::int64_t> position;
      if (body.contains(QLatin1String("PlaybackPositionTicks")))
        position = integer(body.value(QLatin1String("PlaybackPositionTicks")));
      bool changed = pending.has_value();
      const std::int64_t runTime = integer(item->value(QLatin1String("RunTimeTicks")));
      for (const QString& play : std::as_const(plays)) {
        switch (classifyView(last.value(play).positionTicks, runTime)) {
        case ViewOutcome::Played:
          played = true;
          ++playCount;
          position = 0;
          changed = true;
          break;
        case ViewOutcome::Resume:
          position = last.value(play).positionTicks;
          changed = true;
          break;
        case ViewOutcome::Nothing:
          break;
        }
      }
      if (!changed) {
        m_journal.removeItem(m_serverId, m_userId, itemId, plays);
        continue;
      }
      if (position) body.insert(QStringLiteral("PlaybackPositionTicks"), static_cast<double>(*position));
      if (played) {
        body.insert(QStringLiteral("Played"), true);
        body.insert(QStringLiteral("PlayCount"), playCount);
      }
      body.insert(QStringLiteral("LastPlayedDate"), serverDate(latest));
      ViewUpdate update{m_serverId, m_userId, itemId, body, latest};
      // Written before it is sent: a second attempt sends these values.
      if (!m_journal.setUpdate(update, plays)) {
        qCWarning(lcSync) << "cannot write the offline journal; not sent";
        continue;
      }
      m_toSend.append(update);
    }
    sendNext();
  }

  void OfflineSync::sendNext() {
    if (m_toSend.isEmpty()) {
      Q_EMIT journalSynced(m_sent, m_serverWon);
      refreshManifests();
      return;
    }
    const ViewUpdate update = m_toSend.takeFirst();
    const std::uint64_t generation = m_generation;
    api::updateUserItemData(
        m_client, update.itemId, update.body, this,
        [this, generation, update](api::Result<QJsonObject> result) {
          if (generation != m_generation) return;
          if (!result) {
            if (result.error().isNotFound()) {
              m_journal.removeItem(update.serverId, update.userId, update.itemId, {});
              sendNext();
              return;
            }
            // Kept for the next run.
            qCInfo(lcSync) << "journal: not sent:" << result.error().message;
            Q_EMIT journalSynced(m_sent, m_serverWon);
            done();
            return;
          }
          m_journal.removeItem(update.serverId, update.userId, update.itemId, {});
          applyServerData(update.itemId, *result);
          ++m_sent;
          sendNext();
        });
  }

  void OfflineSync::refreshManifests() {
    // Complete items only: the queue keeps and writes the manifests of the
    // others.
    QStringList ids;
    for (const ItemManifest& m : m_catalog.items(m_serverId, m_userId))
      if (m.state == ItemState::Complete) ids.append(m.itemId);
    if (ids.isEmpty()) {
      Q_EMIT manifestsRefreshed(0);
      done();
      return;
    }
    ids.sort();
    fetchItems(ids, {}, [this](bool ok, QHash<QString, QJsonObject> items) {
      if (!ok) return done();
      int changed = 0;
      const QDateTime now = QDateTime::currentDateTimeUtc();
      for (ItemManifest m : m_catalog.items(m_serverId, m_userId)) {
        if (m.state != ItemState::Complete) continue;
        const ItemManifest before = m;
        const auto item = items.constFind(m.itemId);
        if (item == items.constEnd()) {
          m.removedOnServer = true;
        } else {
          m.removedOnServer = false;
          const QJsonObject userData = item->value(QLatin1String("UserData")).toObject();
          if (!userData.isEmpty()) m.userData = userData;
          const QString sourceId = m.mediaSource.value(QLatin1String("Id")).toString();
          const QString eTag = m.mediaSource.value(QLatin1String("ETag")).toString();
          bool sourceFound = false;
          for (const auto& value : item->value(QLatin1String("MediaSources")).toArray()) {
            const QJsonObject source = value.toObject();
            if (source.value(QLatin1String("Id")).toString() != sourceId) continue;
            sourceFound = true;
            const QString serverTag = source.value(QLatin1String("ETag")).toString();
            if (!eTag.isEmpty() && !serverTag.isEmpty() && serverTag != eTag) m.changedOnServer = true;
          }
          if (!sourceFound && item->contains(QLatin1String("MediaSources"))) m.changedOnServer = true;
        }
        const bool differs = m.removedOnServer != before.removedOnServer ||
                             m.changedOnServer != before.changedOnServer || m.userData != before.userData;
        if (!differs) continue;
        ++changed;
        m.refreshedAt = now;
        if (m_store.writeManifest(m)) m_catalog.update(m);
      }
      Q_EMIT manifestsRefreshed(changed);
      done();
    });
  }

  void OfflineSync::applyServerData(const QString& itemId, const QJsonObject& userData) {
    auto manifest = m_catalog.find(m_serverId, m_userId, itemId);
    if (!manifest || manifest->state != ItemState::Complete || userData.isEmpty() ||
        manifest->userData == userData)
      return;
    manifest->userData = userData;
    if (m_store.writeManifest(*manifest)) m_catalog.update(*manifest);
  }

  void OfflineSync::done() {
    m_running = false;
    Q_EMIT finished();
    if (m_again) {
      m_again = false;
      start();
    }
  }

} // namespace jfd::offline
