#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

// The manifest of one downloaded item: <item folder>/item.json. It keeps the
// server's own JSON for the item, the chosen media source and the user data,
// so the offline screens read them with the same parsers as online ones
// (api::json), plus what the downloader needs to resume after a restart.
//
// Secrets never reach the disk: toJson() drops TranscodingUrl,
// DirectStreamUrl and every ApiKey / api_key query parameter (see
// scrubSecrets), whatever the caller passes in.
namespace jfd::offline {

  enum class ItemState {
    // In the queue; nothing or part of the file is on disk (media.part).
    Queued,
    // Stopped by the user; stays in the queue.
    Paused,
    // Gave up after the retries; "Retry" puts it back in the queue.
    Failed,
    // The file is complete, its size checked, renamed from .part.
    Complete,
  };

  QString stateName(ItemState state);
  std::optional<ItemState> stateFromName(const QString& name);

  // A compressed copy (stage 3.10b): which preset the server transcoded it
  // with, and how far the download of its HLS segments got. No URL, no
  // PlaySessionId: a resumed download asks the server again and compares
  // the new playlist with `signature`.
  struct TranscodeState {
    // The preset's id ("video-1080p", "video-720p", "audio-opus128").
    QString preset;
    // The audio track the copy has (a MediaStreams index of the original);
    // -1 until the server chose one.
    int audioStreamIndex = -1;
    // What the server encodes to ("hevc" or "h264"; "aac" or "opus"), from
    // its answer.
    QString videoCodec;
    QString audioCodec;
    // The media playlist: its segment count and the hash of its segment
    // durations; and the hash of the init segment. A resume continues only
    // when all three match.
    int segmentCount = 0;
    QString signature;
    QString initHash;
    // Segments already in media.part, and its size after the last of them.
    int segmentsDone = 0;
    std::int64_t partBytes = 0;

    bool operator==(const TranscodeState&) const = default;
  };

  struct ItemManifest {
    static constexpr int FormatVersion = 1;

    QString serverId;
    QString userId;
    QString itemId;
    ItemState state = ItemState::Queued;
    // "original" (stage 3.10a) or "compressed" (3.10b).
    QString quality = QStringLiteral("original");
    // Only for a compressed copy. Until it is complete, `mediaSource` is the
    // server's source it is made from; then it describes the copy (one
    // video and one audio track inside, text subtitles as files: see
    // compressedMediaSource).
    TranscodeState transcode;
    // The server's JSON: BaseItemDto, MediaSourceInfo, UserItemDataDto.
    QJsonObject item;
    QJsonObject mediaSource;
    QJsonObject userData;
    // The media file's name inside the item folder ("media.mkv").
    QString mediaFile;
    // Expected size in bytes: MediaSourceInfo.Size, else the server's
    // Content-Length / Content-Range total once a download started. 0 when
    // unknown.
    std::int64_t expectedSize = 0;
    // The ETag or Last-Modified of the response the .part file came from;
    // sent back as If-Range when resuming. Empty: start over.
    QString resumeValidator;
    // Queue order: earlier first.
    QDateTime queuedAt;
    QDateTime completedAt;

    // What else the item keeps, fetched once the media file is complete
    // (ExtrasDownloader), as paths relative to the item folder:
    // external subtitles by their MediaStreams index ("subs/0.srt"),
    QMap<int, QString> subtitleFiles;
    // images by type ("Primary", "Backdrop", "Logo", "Thumb" ->
    // "images/Primary"),
    QMap<QString, QString> imageFiles;
    // the lyrics of a track (the body of Audio/{id}/Lyrics) and the media
    // segments of a video (the body of MediaSegments/{id}); empty when the
    // server has none.
    QString lyricsFile;
    QString segmentsFile;
    // All of the above was fetched (or the server has none of it). False:
    // asked again at the next sign-in.
    bool extrasComplete = false;

    // The series, season, album, playlist or collection the user pressed
    // "Download" on to get this item (besides the item's own SeriesId,
    // SeasonId and AlbumId), so the group's page shows what it has.
    QStringList parents;

    // What the last look at the server found (OfflineSync): the item is no
    // longer there (the copy still plays until deleted), or its file changed
    // (the media source's ETag differs; downloading again is up to the
    // user).
    bool removedOnServer = false;
    bool changedOnServer = false;
    QDateTime refreshedAt;
  };

  // The manifest as stored, secrets removed.
  QJsonObject toJson(const ItemManifest& manifest);
  // Nullopt when the JSON is not a manifest of a known format version or
  // lacks an id.
  std::optional<ItemManifest> manifestFromJson(const QJsonObject& object);

  // A copy of `object` (any depth) without TranscodingUrl, DirectStreamUrl,
  // and with ApiKey / api_key / X-Emby-Token parameters cut from every
  // string that looks like a URL or a query.
  QJsonObject scrubSecrets(const QJsonObject& object);

  // The extension of the media file for a source: its container's first
  // name ("mkv" for "mkv,webm"), or the extension of its server path, or
  // "bin". Only [a-z0-9], at most 8 characters.
  QString mediaExtension(const QJsonObject& mediaSource);

} // namespace jfd::offline
