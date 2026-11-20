#pragma once

#include <jfd/offline/Manifest.h>

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

// Where downloads live on disk. Under the root (the "Download folder"
// setting), format 2 (ItemManifest::format):
//
//   <root>/.jellyfin-desktop/<serverId>/<userId>/<itemId>/item.json   the manifest
//   <root>/.jellyfin-desktop/<serverId>/<userId>/<itemId>/media.part  while downloading
//   <root>/.jellyfin-desktop/<serverId>/<userId>/<itemId>/images/...  images, lyrics, segments
//   <root>/<library path>.<ext>, its subtitles and audio tracks       once complete
//
// The library paths are the readable names of LibraryNaming.h ("Shows/
// Series/Season 01/Series - S01E02 - Title.mkv"), so other players and a
// Jellyfin or Kodi library can use the folder as it is. Format 1 (before)
// kept everything in <root>/<serverId>/<userId>/<itemId>; migrate() moves
// such items. Internal folder names are ids. An item is complete only when
// its manifest says so, and the manifest says so only after the file's size
// was checked and the .part renamed. Manifests are replaced atomically
// (QSaveFile).
namespace jfd::offline {

  class DownloadStore {
  public:
    explicit DownloadStore(QString root);

    const QString& root() const { return m_root; }

    // Server, user and item ids are what Jellyfin uses (32 hex digits, or a
    // GUID with dashes); anything else (separators, "..") is refused so a
    // hostile id cannot leave the root.
    static bool isSafeId(const QString& id);

    // The folder of the internal files (manifest, .part, images).
    // Format 2; the overload taking a manifest follows its format.
    QString itemDir(const QString& serverId, const QString& userId, const QString& itemId) const;
    QString itemDir(const ItemManifest& manifest) const;
    // Format 1's folder of an item.
    QString legacyItemDir(const QString& serverId, const QString& userId, const QString& itemId) const;
    // <root>/.jellyfin-desktop
    QString internalRoot() const;
    QString manifestPath(const ItemManifest& manifest) const;
    QString partPath(const ItemManifest& manifest) const;
    // Empty while the manifest names no media file.
    QString mediaPath(const ItemManifest& manifest) const;
    // The absolute path of a file the manifest names (mediaFile, a value of
    // subtitleFiles or audioFiles): relative to the item folder (format 1)
    // or to the root (format 2). Images, lyrics and segments are always in
    // the item folder.
    QString filePath(const ItemManifest& manifest, const QString& file) const;

    // A library path for the media of `item` with `extension` that no file
    // on disk and no path in `taken` (lower case) has: LibraryNaming's
    // name, or the same with " [<first 8 of the id>]" added.
    QString libraryFileFor(const QJsonObject& item, const QString& itemId, const QString& extension,
                           const QSet<QString>& taken) const;

    // Moves a format 1 item to format 2: its folder to the internal tree,
    // its media and subtitles to the library. Something that cannot be
    // moved (a file another program has open) stays where it is and the
    // manifest says so; the item keeps playing either way. Nullopt when the
    // folder itself could not be moved (the item stays format 1, tried again
    // at the next start). `taken`: library paths in use (lower case), the
    // new one is added.
    std::optional<ItemManifest> migrate(const ItemManifest& manifest, QSet<QString>& taken) const;

    // Creates the item's folder and writes item.json atomically. False on an
    // unsafe id or a disk error.
    bool writeManifest(const ItemManifest& manifest) const;
    std::optional<ItemManifest> readManifest(const QString& serverId, const QString& userId,
                                             const QString& itemId) const;
    // Every readable manifest under the root. Folders without one, or with
    // one that does not parse, are skipped (and left alone).
    QList<ItemManifest> scan() const;

    // Deletes the item's folder and its library files (and the library
    // folders this leaves empty). False when something could not be deleted
    // (on Windows, a file another program has open).
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
