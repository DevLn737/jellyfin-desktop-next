#pragma once

#include <jfd/offline/Manifest.h>

#include <QList>
#include <QString>

#include <optional>

// Where downloads live on disk. Under the root (the "Download folder"
// setting):
//
//   <root>/<serverId>/<userId>/<itemId>/item.json    the manifest
//   <root>/<serverId>/<userId>/<itemId>/media.part   while downloading
//   <root>/<serverId>/<userId>/<itemId>/media.<ext>  once complete
//
// Names are ids, never titles: no characters Windows forbids, and the
// shortest paths. An item is complete only when its manifest says so, and
// the manifest says so only after the file's size was checked and the .part
// renamed. Manifests are replaced atomically (QSaveFile).
namespace jfd::offline {

  class DownloadStore {
  public:
    explicit DownloadStore(QString root);

    const QString& root() const { return m_root; }

    // Server, user and item ids are what Jellyfin uses (32 hex digits, or a
    // GUID with dashes); anything else (separators, "..") is refused so a
    // hostile id cannot leave the root.
    static bool isSafeId(const QString& id);

    QString itemDir(const QString& serverId, const QString& userId, const QString& itemId) const;
    QString itemDir(const ItemManifest& manifest) const;
    QString manifestPath(const ItemManifest& manifest) const;
    QString partPath(const ItemManifest& manifest) const;
    // Empty while the manifest names no media file.
    QString mediaPath(const ItemManifest& manifest) const;

    // Creates the item's folder and writes item.json atomically. False on an
    // unsafe id or a disk error.
    bool writeManifest(const ItemManifest& manifest) const;
    std::optional<ItemManifest> readManifest(const QString& serverId, const QString& userId,
                                             const QString& itemId) const;
    // Every readable manifest under the root. Folders without one, or with
    // one that does not parse, are skipped (and left alone).
    QList<ItemManifest> scan() const;

    // Deletes the item's folder. False when something in it could not be
    // deleted (on Windows, a file another program has open).
    bool removeItem(const ItemManifest& manifest) const;

    // The path to give another program (mpv) for a file of ours. On Windows,
    // an absolute path of MAX_PATH (260) characters or more gets the "\\?\"
    // prefix (and "\\?\UNC\" for a network share), with native separators;
    // Qt adds it by itself for its own file access. Elsewhere unchanged.
    static QString externalPath(const QString& path);
    // The same rule on any system, for tests.
    static QString windowsExternalPath(const QString& path);

  private:
    QString m_root;
  };

} // namespace jfd::offline
