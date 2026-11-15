#pragma once

#include <jfd/api/Models.h>
#include <jfd/api/Result.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

class QObject;

// What the offline downloads (jfd::offline) need from the server: where an
// original file comes from, which kind of download a user may make, and the
// rule that the session's token goes only to its own server.
namespace jfd::api {

  class JellyfinClient;

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

  // --- What a downloaded item keeps besides its file ---------------------

  // GET Videos/{itemId}/{mediaSourceId}/Subtitles/{index}/Stream.{format}:
  // an external subtitle file, as the server keeps it.
  QUrl subtitleDownloadUrl(const QUrl& serverUrl, const QString& itemId,
                           const QString& mediaSourceId, int streamIndex, const QString& format);
  // The file format to ask for a text subtitle codec ("subrip" -> "srt",
  // "ass", "ssa", "webvtt" -> "vtt"); empty for picture subtitles (PGS,
  // VobSub) and unknown codecs, which are not downloaded.
  QString textSubtitleFormat(const QString& codec);
  // GET Items/{itemId}/Images/{type}[/{index}]?tag=..&maxWidth=..: an image
  // no wider than `maxWidth`.
  QUrl imageDownloadUrl(const QUrl& serverUrl, const QString& itemId, const QString& type,
                        std::optional<int> index, const QString& tag, int maxWidth);
  // GET Audio/{itemId}/Lyrics (404: the track has none).
  QUrl lyricsDownloadUrl(const QUrl& serverUrl, const QString& itemId);
  // GET MediaSegments/{itemId} (intros and credits, for skipping offline).
  QUrl mediaSegmentsDownloadUrl(const QUrl& serverUrl, const QString& itemId);

  // --- Watched state, for the offline journal ----------------------------

  // GET UserItems/{itemId}/UserData: the signed-in user's UserItemDataDto
  // (PlaybackPositionTicks, PlayCount, Played, LastPlayedDate, ...).
  void fetchUserItemData(JellyfinClient& client, const QString& itemId, QObject* context,
                         std::function<void(Result<QJsonObject>)> done);
  // POST UserItems/{itemId}/UserData with an UpdateUserItemDataDto. The
  // server stores what it is sent: LastPlayedDate as given (checked on
  // 12.2) and PlayCount as a value, so sending the same body twice changes
  // nothing. (POST UserPlayedItems would add one to PlayCount each time.)
  // Answers the stored UserItemDataDto.
  void updateUserItemData(JellyfinClient& client, const QString& itemId, const QJsonObject& body,
                          QObject* context, std::function<void(Result<QJsonObject>)> done);
  // GET Items?ids=..: the signed-in user's view of these items (UserData,
  // RunTimeTicks, MediaType, MediaSources with their ETag). Items the
  // server no longer has are missing from the answer.
  void fetchItemsById(JellyfinClient& client, const QStringList& ids, QObject* context,
                      std::function<void(Result<QJsonArray>)> done);

  // GET Items?ParentId=..&Recursive=true: what a "Download" on a series,
  // season, album, playlist or collection takes: its playable items (films,
  // episodes, videos, tracks) with their MediaSources and UserData. Series
  // and albums come in their order (season, number); a playlist keeps its
  // own. `unplayedOnly` leaves out what the user has watched.
  void fetchDownloadableChildren(JellyfinClient& client, const QString& parentId, bool keepOrder,
                                 bool unplayedOnly, QObject* context,
                                 std::function<void(Result<QJsonArray>)> done);

} // namespace jfd::api
