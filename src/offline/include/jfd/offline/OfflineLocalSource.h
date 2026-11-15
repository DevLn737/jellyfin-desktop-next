#pragma once

#include <jfd/api/MusicApi.h>
#include <jfd/offline/DownloadStore.h>
#include <jfd/offline/OfflineCatalog.h>
#include <jfd/session/LocalSource.h>

#include <QString>

#include <optional>

// session::LocalSource on the download folder: the launcher asks it before
// the server, and it answers with a complete copy of the signed-in user's
// item when one should play from disk. It reads only the catalog and the
// item folder, never the network.
namespace jfd::offline {

  class OfflineLocalSource final : public session::LocalSource {
  public:
    OfflineLocalSource(const DownloadStore& store, const OfflineCatalog& catalog)
        : m_store(store), m_catalog(catalog) {}

    // Whose downloads; empty ids (signed out) find nothing.
    void setUser(const QString& serverId, const QString& userId) {
      m_serverId = serverId;
      m_userId = userId;
    }
    // Whether the server can be reached now (the session socket is Ready).
    void setOnline(bool online) { m_online = online; }
    bool online() const { return m_online; }
    // "Play downloads from disk" (downloads/preferLocal, on by default):
    // whether a copy is used while the server can be reached.
    void setPreferLocal(bool prefer) { m_preferLocal = prefer; }

    // Without the online and preference checks: the complete copy, if its
    // media file is there.
    std::optional<session::LocalCopy> copy(const QString& itemId) const;
    std::optional<session::LocalCopy> find(const QString& itemId) const override;

    // A downloaded track's lyrics: nullopt when the track is not downloaded
    // (ask the server), empty lyrics when it is and has none.
    std::optional<api::Lyrics> lyrics(const QString& itemId) const;

  private:
    const DownloadStore& m_store;
    const OfflineCatalog& m_catalog;
    QString m_serverId;
    QString m_userId;
    bool m_online = true;
    bool m_preferLocal = true;
  };

} // namespace jfd::offline
