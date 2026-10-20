#pragma once

#include <jfd/api/Models.h>
#include <jfd/api/Result.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <optional>

// Turning a PlaybackInfo answer into URLs mpv can open, and the bodies of
// playback reports. Pure functions, so the stream decision is tested without
// a server.
namespace jfd::api {

  struct PlaybackInfoRequest {
    QString itemId;
    QString userId;
    QString mediaSourceId;
    std::optional<std::int64_t> startTimeTicks;
    std::optional<int> audioStreamIndex;
    // -1 disables subtitles.
    std::optional<int> subtitleStreamIndex;
    std::optional<std::int64_t> maxStreamingBitrate;
    bool enableDirectPlay = true;
    bool enableDirectStream = true;
    bool enableTranscoding = true;
    bool allowVideoStreamCopy = true;
    bool allowAudioStreamCopy = true;
    QJsonObject deviceProfile;
  };

  QJsonObject playbackInfoBody(const PlaybackInfoRequest& request);

  enum class PlayMethod { DirectPlay, DirectStream, Transcode };
  QString playMethodName(PlayMethod method);

  struct ExternalTrack {
    // Index in MediaStreams, for reporting and track menus.
    int streamIndex = -1;
    QUrl url;
    QString codec;
    QString language;
    QString title;
    bool isDefault = false;
    bool isForced = false;
  };

  struct PlaybackStream {
    QUrl url;
    PlayMethod method = PlayMethod::DirectStream;
    bool isHls = false;
    QString itemId;
    QString mediaSourceId;
    QString playSessionId;
    QList<ExternalTrack> externalSubtitles;
    QList<ExternalTrack> externalAudio;
  };

  struct StreamContext {
    // Server base URL including any path prefix, for example
    // https://example.org/jellyfin.
    QUrl serverUrl;
    QString accessToken;
    QString deviceId;
  };

  // Chooses how to play `source`: direct play of a remote HTTP source, then a
  // static stream of the original file, then the server's transcode. Media
  // URLs carry the token as `ApiKey`, since mpv cannot be given our headers
  // for one origin only; `api_key` is never generated. A source that needs
  // extra HTTP headers is never played directly.
  Result<PlaybackStream> resolvePlaybackStream(const StreamContext& context, const QString& itemId,
                                               const PlaybackInfo& info, const MediaSource& source);

  // mpv numbers embedded tracks of each type from 1 in file order, while
  // Jellyfin's Index counts every stream of every type, external ones
  // included. Returns the mpv track id for an embedded stream, or nullopt for
  // an external or unknown index.
  std::optional<int> embeddedTrackId(const MediaSource& source, int streamIndex);

  struct PlaybackProgress {
    QString itemId;
    QString mediaSourceId;
    QString playSessionId;
    PlayMethod playMethod = PlayMethod::DirectStream;
    std::int64_t positionTicks = 0;
    bool isPaused = false;
    bool isMuted = false;
    int volumeLevel = 100;
    std::optional<int> audioStreamIndex;
    std::optional<int> subtitleStreamIndex;
    double playbackRate = 1.0;
    // "TimeUpdate", "Pause", "Unpause", "AudioTrackChange", ... or empty.
    QString eventName;
  };

  QJsonObject playbackStartBody(const PlaybackProgress& progress);
  QJsonObject playbackProgressBody(const PlaybackProgress& progress);
  QJsonObject playbackStoppedBody(const PlaybackProgress& progress, bool failed);

} // namespace jfd::api
