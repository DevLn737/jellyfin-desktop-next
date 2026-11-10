#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

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

  struct ItemManifest {
    static constexpr int FormatVersion = 1;

    QString serverId;
    QString userId;
    QString itemId;
    ItemState state = ItemState::Queued;
    // "original" (stage 3.10a) or "compressed" (3.10b).
    QString quality = QStringLiteral("original");
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
