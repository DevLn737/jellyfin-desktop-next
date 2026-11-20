#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Names of downloaded files in the readable library, as Jellyfin, Kodi and
// most players expect them (docs/offline.md, "Library layout"):
//
//   Movies/Name (2024)/Name (2024).mkv
//   Movies/Name (2024)/Name (2024).ru.forced.srt
//   Movies/Name (2024)/Name (2024).ru.mka
//   Shows/Series/Season 01/Series - S01E02 - Title.mkv   (+ the same sidecars)
//   Music/Album Artist/Album/01 - Title.flac
//   Videos/Name/Name.mp4
//
// Paths are relative to the download folder, with '/' separators, and every
// part is a safe file name on Windows and Linux.
namespace jfd::offline {

  // A file name part without characters Windows forbids (<>:"/\|?* and
  // control characters), without trailing dots and spaces, not a reserved
  // device name (CON, NUL, COM1...), at most `maxLength` characters. Empty
  // input gives `fallback`.
  QString safeFileName(const QString& name, const QString& fallback = QStringLiteral("Untitled"),
                       int maxLength = 100);

  // Where the media file of `item` (the server's BaseItemDto) goes, without
  // the extension: "Shows/Series/Season 01/Series - S01E02 - Title".
  QString libraryBase(const QJsonObject& item);

  // The suffix of an external track's file after the video's name, for a
  // sidecar next to the downloaded media ("ru.forced", "en", "Commentary.en").
  // When the server's own file names are known (`mediaPath`, the source's
  // Path, and `stream`'s Path) and the track's name starts with the video's,
  // the server's suffix is kept as it is, so the copy is named as on the
  // server. Otherwise it is made from the stream's language, title and flags
  // (Default, Forced, IsHearingImpaired), the way Jellyfin reads them back.
  // Empty when there is nothing to say (no language, no title, no flags).
  QString sidecarSuffix(const QJsonObject& stream, const QString& mediaPath);

  // `base` + "." + `suffix` + "." + `extension`, leaving out an empty suffix,
  // with a number added ("base.2.ru.srt") when the name is in `taken`.
  QString sidecarPath(const QString& base, const QString& suffix, const QString& extension,
                      const QStringList& taken);

} // namespace jfd::offline
