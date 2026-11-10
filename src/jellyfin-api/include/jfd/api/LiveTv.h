#pragma once

#include <jfd/api/JellyfinClient.h>

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Live TV (package 3.7a): the server's tuners and guide as channels and
// programmes, read through the LiveTv API of Jellyfin 12.2. Recordings and
// timers (3.7b) are not here yet. Channels play like any item: PlaybackInfo
// opens the live stream (AutoOpenLiveStream) and answers its LiveStreamId,
// which the playback reports carry; the server closes the stream when the
// Stopped report names it. closeLiveStream() is for a stream that was opened
// but never reported.
namespace jfd::api {

  // One Live TV service (the built-in "Emby" one for tuners and guides).
  struct LiveTvService {
    QString name;
    // "Ok" or "Unavailable".
    QString status;
    QString statusMessage;
    bool isVisible = true;
  };

  // GET LiveTv/Info.
  struct LiveTvInfo {
    bool isEnabled = false;
    QList<LiveTvService> services;
    // Users who may watch; the server leaves it empty for all users when
    // the access policy says so [unverified on 12.2: the stand's admin user
    // is listed].
    QStringList enabledUsers;

    // Live TV is set up and this user may use it: a service answers "Ok"
    // and the user is listed (or nobody is).
    bool availableTo(const QString& userId) const;
  };

  // One programme in the guide (BaseItemDto of type Program).
  struct LiveTvProgram {
    QString id;
    QString channelId;
    QString name;
    QString episodeTitle;
    QString overview;
    QDateTime start; // UTC
    QDateTime end;   // UTC
    bool isLive = false;
    bool isNews = false;
    bool isMovie = false;
    bool isSeries = false;

    bool isValid() const { return start.isValid() && end.isValid() && end > start; }
    // On the air at `now`: start <= now < end.
    bool isOnAt(const QDateTime& now) const;
    // How far the programme is at `now`, 0..1 (0 before it, 1 after it).
    double progressAt(const QDateTime& now) const;
  };

  // A channel (BaseItemDto of type TvChannel).
  struct LiveTvChannel {
    QString id;
    QString name;
    // The tuner's number ("1", "2.1"); may be empty.
    QString number;
    QString primaryImageTag;
    bool isFavorite = false;
    // With ChannelQuery::addCurrentProgram: what is on now, if the guide
    // knows.
    std::optional<LiveTvProgram> currentProgram;
  };

  struct LiveTvChannelPage {
    QList<LiveTvChannel> channels;
    int totalRecordCount = 0;
    int startIndex = 0;
  };

  struct LiveTvProgramPage {
    QList<LiveTvProgram> programs;
    int totalRecordCount = 0;
    int startIndex = 0;
  };

  struct LiveTvChannelQuery {
    int startIndex = 0;
    int limit = 200;
    bool addCurrentProgram = true;
    std::optional<bool> isFavorite;
  };

  // A time window of the guide. Programmes that end after `from` and start
  // before `to` are listed, sorted by channel and start.
  struct LiveTvProgramQuery {
    QStringList channelIds;
    QDateTime from;
    QDateTime to;
    int startIndex = 0;
    int limit = 500;
  };

  namespace json {
    Result<LiveTvInfo> liveTvInfo(const QJsonObject& object);
    Result<LiveTvProgram> liveTvProgram(const QJsonObject& object);
    Result<LiveTvChannel> liveTvChannel(const QJsonObject& object);
    Result<LiveTvChannelPage> liveTvChannelPage(const QJsonObject& object);
    Result<LiveTvProgramPage> liveTvProgramPage(const QJsonObject& object);
    // A server date ("2026-11-10T16:00:00.0000000Z") as a UTC QDateTime;
    // invalid when it is missing or has no zone.
    QDateTime serverDate(const QJsonValue& value);
  } // namespace json

  class LiveTvApi {
  public:
    explicit LiveTvApi(JellyfinClient& client) : m_client(client) {}

    // GET LiveTv/Info.
    void info(QObject* context, Callback<LiveTvInfo> done);
    // GET LiveTv/Channels, in the server's channel order (number, then name).
    void channels(const LiveTvChannelQuery& query, QObject* context, Callback<LiveTvChannelPage> done);
    // GET LiveTv/Programs for a window of the guide, one page.
    void programs(const LiveTvProgramQuery& query, QObject* context, Callback<LiveTvProgramPage> done);
    // POST LiveStreams/Close.
    void closeLiveStream(const QString& liveStreamId, QObject* context, Callback<Empty> done);

    // `2026-11-10T16:00:00.000Z`, as the date filters take it.
    static QString queryDate(const QDateTime& when);

  private:
    JellyfinClient& m_client;
  };

} // namespace jfd::api
