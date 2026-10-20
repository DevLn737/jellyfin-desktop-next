#pragma once

#include <jfd/api/Result.h>
#include <jfd/api/ServerTime.h>
#include <jfd/api/ServerVersion.h>

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
    std::int64_t playbackPositionTicks = 0;
    bool played = false;
    int playCount = 0;
  };

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
    QString primaryImageTag;
    UserItemData userData;
  };

  struct ItemPage {
    QList<Item> items;
    int totalRecordCount = 0;
    int startIndex = 0;
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
    Result<MediaStream> mediaStream(const QJsonObject& object);
    Result<MediaSource> mediaSource(const QJsonObject& object);
    Result<PlaybackInfo> playbackInfo(const QJsonObject& object);
    Result<SyncPlayGroup> syncPlayGroup(const QJsonObject& object);
  } // namespace json

} // namespace jfd::api
