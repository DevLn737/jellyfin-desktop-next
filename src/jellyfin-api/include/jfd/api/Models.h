#pragma once

#include <jfd/api/Result.h>
#include <jfd/api/ServerTime.h>
#include <jfd/api/ServerVersion.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

// Typed views of the server responses this client uses. Only the fields the
// client reads are kept; parsing fails when a required field is missing and
// leaves optional ones empty, so a newer server with extra fields still works.
namespace jfd::api {

  struct PublicServerInfo {
    QString id;
    QString name;
    QString versionText;
    ServerVersion version;
    bool startupWizardCompleted = true;
  };

  struct UserInfo {
    QString id;
    QString name;
    QString serverId;
    bool hasPassword = true;
    // SyncPlay access policy: "CreateAndJoinGroups", "JoinGroups" or "None".
    QString syncPlayAccess;
  };

  struct AuthenticationResult {
    QString accessToken;
    QString serverId;
    QString sessionId;
    UserInfo user;
  };

  struct QuickConnectState {
    QString secret;
    QString code;
    bool authenticated = false;
  };

  struct UserItemData {
    // Set on the copies the server sends alone (favorite answers, the
    // UserDataChanged message); empty inside an item.
    QString itemId;
    std::int64_t playbackPositionTicks = 0;
    bool played = false;
    int playCount = 0;
    bool isFavorite = false;
    // Folders only: episodes or items not yet watched.
    std::optional<int> unplayedItemCount;
    std::optional<double> playedPercentage;
  };

  // One trickplay resolution of a media source: tiles of `tileWidth` x
  // `tileHeight` thumbnails, one thumbnail every `intervalMs`. Tile `n` holds
  // thumbnails n * tileWidth * tileHeight onwards, row by row.
  struct TrickplayInfo {
    int width = 0;
    int height = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int thumbnailCount = 0;
    int intervalMs = 0;
    int bandwidth = 0;

    int thumbnailsPerTile() const { return tileWidth * tileHeight; }
    int tileCount() const {
      const int perTile = thumbnailsPerTile();
      return perTile > 0 ? (thumbnailCount + perTile - 1) / perTile : 0;
    }
  };

  // Where the thumbnail for a playback position sits: tile image `tileIndex`,
  // and the pixel rectangle inside it.
  struct TrickplayFrame {
    int tileIndex = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  // The frame shown at `positionMs`; nullopt when the info is empty.
  std::optional<TrickplayFrame> trickplayFrame(const TrickplayInfo& info, std::int64_t positionMs);

  struct Item {
    QString id;
    QString name;
    // Jellyfin BaseItemKind: "Movie", "Series", "Season", "Episode", ...
    QString type;
    QString mediaType;
    bool isFolder = false;
    QString seriesId;
    QString seriesName;
    QString seasonId;
    QString seasonName;
    std::optional<int> indexNumber;
    std::optional<int> parentIndexNumber;
    std::optional<int> productionYear;
    std::optional<std::int64_t> runTimeTicks;
    QString overview;
    QStringList genres;
    std::optional<double> communityRating;
    QString officialRating;
    // Views only: "movies", "tvshows", "music", "boxsets", ...
    QString collectionType;
    std::optional<int> childCount;
    // Series only: "Continuing" or "Ended".
    QString status;
    std::optional<double> primaryImageAspectRatio;
    // Image tags by type ("Primary", "Thumb", "Logo", "Banner"); backdrops
    // are a list of their own.
    QHash<QString, QString> imageTags;
    QStringList backdropImageTags;
    QString primaryImageTag;
    // Images an episode or season borrows from its series.
    QString seriesPrimaryImageTag;
    QString parentBackdropItemId;
    QStringList parentBackdropImageTags;
    QString parentThumbItemId;
    QString parentThumbImageTag;
    // Asked for with the Trickplay field: resolutions by media source id.
    QHash<QString, QList<TrickplayInfo>> trickplay;
    UserItemData userData;
  };

  struct ItemPage {
    QList<Item> items;
    int totalRecordCount = 0;
    int startIndex = 0;
  };

  struct ItemFilters {
    QStringList genres;
    QList<int> years;
    QStringList officialRatings;
  };

  // A skippable part of an item (MediaSegments API, Jellyfin 10.10+).
  struct MediaSegment {
    QString id;
    QString itemId;
    // "Intro", "Outro", "Recap", "Preview", "Commercial", "Unknown".
    QString type;
    std::int64_t startTicks = 0;
    std::int64_t endTicks = 0;
  };

  struct MediaStream {
    // Index in the source's MediaStreams list (not an mpv track id).
    int index = -1;
    // "Video", "Audio", "Subtitle", ...
    QString type;
    QString codec;
    QString language;
    QString title;
    QString displayTitle;
    bool isDefault = false;
    bool isForced = false;
    bool isExternal = false;
    // "Encode", "Embed", "External", "Hls", "Drop" for subtitles.
    QString deliveryMethod;
    QString deliveryUrl;
    bool isExternalUrl = false;
    std::optional<double> realFrameRate;
    std::optional<double> averageFrameRate;
    std::optional<int> channels;
  };

  struct MediaSource {
    QString id;
    // "File", "Http", ...
    QString protocol;
    QString path;
    QString container;
    QString eTag;
    bool supportsDirectPlay = false;
    bool supportsDirectStream = false;
    bool supportsTranscoding = false;
    QString transcodingUrl;
    // "http" or "hls".
    QString transcodingSubProtocol;
    std::optional<std::int64_t> runTimeTicks;
    std::optional<int> defaultAudioStreamIndex;
    std::optional<int> defaultSubtitleStreamIndex;
    // Headers the source needs. Not forwarded (see the contract's
    // authentication.requiredSourceHeaders), so a source with any is not
    // played directly.
    QJsonObject requiredHttpHeaders;
    QList<MediaStream> mediaStreams;
  };

  struct PlaybackInfo {
    QList<MediaSource> mediaSources;
    QString playSessionId;
    // Set when the server refuses playback: "NotAllowed", "NoCompatibleStream",
    // "RateLimitExceeded".
    QString errorCode;
  };

  struct SyncPlayGroup {
    QString groupId;
    QString groupName;
    // "Idle", "Waiting", "Paused", "Playing".
    QString state;
    QStringList participants;
    std::optional<core::ServerInstant> lastUpdatedAt;
  };

  // One GetUtcTime round trip. Local monotonic times are taken immediately
  // before the request is sent and when the reply arrives; TimeSync in the
  // syncplay module turns samples into a core::ClockOffset.
  struct UtcTimeSample {
    core::LocalInstant requestSent;
    core::ServerInstant requestReception;
    core::ServerInstant responseTransmission;
    core::LocalInstant responseReceived;
  };

  namespace json {
    Result<PublicServerInfo> publicServerInfo(const QJsonObject& object);
    Result<UserInfo> user(const QJsonObject& object);
    Result<AuthenticationResult> authenticationResult(const QJsonObject& object);
    Result<QuickConnectState> quickConnectState(const QJsonObject& object);
    Result<Item> item(const QJsonObject& object);
    Result<ItemPage> itemPage(const QJsonObject& object);
    // A bare JSON array of items, as Items/Latest answers.
    Result<ItemPage> itemArray(const QJsonArray& array);
    Result<UserItemData> userItemData(const QJsonObject& object);
    Result<ItemFilters> itemFilters(const QJsonObject& object);
    Result<QList<MediaSegment>> mediaSegments(const QJsonObject& object);
    Result<MediaStream> mediaStream(const QJsonObject& object);
    Result<MediaSource> mediaSource(const QJsonObject& object);
    Result<PlaybackInfo> playbackInfo(const QJsonObject& object);
    Result<SyncPlayGroup> syncPlayGroup(const QJsonObject& object);
  } // namespace json

} // namespace jfd::api
