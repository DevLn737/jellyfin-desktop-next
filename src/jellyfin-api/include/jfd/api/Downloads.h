#pragma once

#include <jfd/api/Models.h>

#include <QJsonObject>
#include <QString>
#include <QUrl>

// What the offline downloads (jfd::offline) need from the server: where an
// original file comes from, which kind of download a user may make, and the
// rule that the session's token goes only to its own server.
namespace jfd::api {

  // GET Items/{id}/Download: the original file of one version of an item.
  // An item with several versions has one media source per version, and
  // each version is an item of its own: pass the media source's id.
  QUrl originalDownloadUrl(const QUrl& serverUrl, const QString& mediaSourceId);

  // True when `target` has the scheme, host and port of `serverUrl`: only
  // such URLs may carry the session's Authorization header.
  bool isServerUrl(const QUrl& serverUrl, const QUrl& target);

  enum class DownloadQuality {
    // Nothing can be downloaded: the user may neither download nor transcode.
    None,
    // The original file, byte for byte.
    Original,
    // A copy the server transcodes (stage 3.10b).
    Compressed,
  };

  // The quality a download starts with: the original when the user may
  // download files (EnableContentDownloading), otherwise a compressed copy
  // when the server may transcode for the user, otherwise none.
  DownloadQuality defaultDownloadQuality(const UserInfo& user, bool video);

  // Whether the original of this source can be downloaded at all. Folders
  // (Blu-ray and DVD structures), ISO images, remote and .strm sources have
  // no single file to fetch. `item` and `source` are the server's JSON.
  bool originalAvailable(const QJsonObject& item, const QJsonObject& source);

} // namespace jfd::api
