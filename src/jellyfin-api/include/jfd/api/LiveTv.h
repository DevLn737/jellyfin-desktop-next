#pragma once

#include <jfd/api/JellyfinClient.h>

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Live TV (package 3.7): the server's tuners and guide as channels and
// programmes, read through the LiveTv API of Jellyfin 12.2, and its
// recordings and timers (the server's own DVR). Channels play like any item: PlaybackInfo
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
    // The server's timer recording this programme, and the series timer
    // that made it; empty when nothing records it.
    QString timerId;
    QString seriesTimerId;

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
    // Entries the server sent, with the undated ones `programs` leaves out.
    int itemCount = 0;
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

  // A scheduled recording (TimerInfoDto, Type "Timer") or a series
  // recording (SeriesTimerInfoDto, Type "SeriesTimer").
  struct LiveTvTimer {
    QString id;
    QString channelId;
    QString channelName;
    QString programId;
    QString name;
    QString overview;
    QDateTime start; // UTC
    QDateTime end;   // UTC
    // A timer's RecordingStatus: "New", "InProgress", "Completed",
    // "Cancelled", "ConflictedOk", "ConflictedNotOk", "Error". Empty for a
    // series timer.
    QString status;
    // The series timer a timer belongs to.
    QString seriesTimerId;

    bool isRecording() const { return status == QLatin1String("InProgress"); }
    // Still to record or recording now.
    bool isScheduled() const;
  };

  // A recording (LiveTv/Recordings): one in progress is an item of type
  // "Recording"; a finished one is a library item ("Movie", "Episode",
  // "Video") in the server's Recordings folder and plays like any item.
  struct LiveTvRecording {
    QString id;
    QString type;
    QString name;
    // An episode's series; the name is then the episode's.
    QString seriesName;
    QString overview;
    QString channelName;
    // "InProgress" while it records; empty once it is done.
    QString status;
    QString timerId;
    // The programme's times while it records, the file's date after.
    QDateTime start; // UTC
    QDateTime end;   // UTC
    QDateTime dateCreated; // UTC
    qint64 runTimeTicks = 0;
    qint64 playbackPositionTicks = 0;
    bool played = false;
    QString primaryImageTag;

    bool isInProgress() const { return status == QLatin1String("InProgress"); }
  };

  struct LiveTvRecordingPage {
    QList<LiveTvRecording> recordings;
    int totalRecordCount = 0;
    int startIndex = 0;
  };

  struct LiveTvRecordingQuery {
    int startIndex = 0;
    int limit = 200;
    std::optional<bool> isInProgress;
  };

  struct LiveTvTimerQuery {
    QString channelId;
    QString seriesTimerId;
    std::optional<bool> isActive;
    std::optional<bool> isScheduled;
  };

  namespace json {
    Result<LiveTvInfo> liveTvInfo(const QJsonObject& object);
    Result<LiveTvProgram> liveTvProgram(const QJsonObject& object);
    Result<LiveTvChannel> liveTvChannel(const QJsonObject& object);
    Result<LiveTvChannelPage> liveTvChannelPage(const QJsonObject& object);
    Result<LiveTvProgramPage> liveTvProgramPage(const QJsonObject& object);
    Result<LiveTvTimer> liveTvTimer(const QJsonObject& object);
    // A QueryResult of timers or series timers.
    Result<QList<LiveTvTimer>> liveTvTimerList(const QJsonObject& object);
    Result<LiveTvRecording> liveTvRecording(const QJsonObject& object);
    Result<LiveTvRecordingPage> liveTvRecordingPage(const QJsonObject& object);
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
    // Every channel, page after page from query.startIndex (query.limit a
    // page); fails with the first page that fails.
    void allChannels(const LiveTvChannelQuery& query, QObject* context,
                     Callback<QList<LiveTvChannel>> done);
    // GET LiveTv/Channels/{id}: one channel with what is on now.
    void channel(const QString& channelId, QObject* context, Callback<LiveTvChannel> done);
    // GET LiveTv/Programs for a window of the guide, one page.
    void programs(const LiveTvProgramQuery& query, QObject* context, Callback<LiveTvProgramPage> done);
    // The whole window, page after page (query.limit a page), by start.
    void allPrograms(const LiveTvProgramQuery& query, QObject* context,
                     Callback<QList<LiveTvProgram>> done);
    // POST LiveStreams/Close.
    void closeLiveStream(const QString& liveStreamId, QObject* context, Callback<Empty> done);

    // Recordings and timers. Listing needs Live TV access; creating,
    // cancelling and deleting need the LiveTvManagement policy
    // (Policy.EnableLiveTvManagement, or an administrator) and fail with
    // 403 without it.

    // GET LiveTv/Recordings, newest first as the server sorts them.
    void recordings(const LiveTvRecordingQuery& query, QObject* context,
                    Callback<LiveTvRecordingPage> done);
    // DELETE Items/{id}: a finished recording and its file. (12.2's DELETE
    // LiveTv/Recordings/{id} only drops the library entry and leaves the
    // file, so the next scan of the Recordings folder brings it back; seen
    // on the extras stand.) Needs the user's permission to delete content
    // (an administrator has it). One in progress is stopped with
    // cancelTimer() instead.
    void deleteRecording(const QString& recordingId, QObject* context, Callback<Empty> done);
    // GET LiveTv/Timers: the recordings to come and those recording now.
    void timers(const LiveTvTimerQuery& query, QObject* context, Callback<QList<LiveTvTimer>> done);
    // GET LiveTv/SeriesTimers.
    void seriesTimers(QObject* context, Callback<QList<LiveTvTimer>> done);
    // GET LiveTv/Timers/Defaults?programId=: the server's timer for a
    // programme (a SeriesTimerInfoDto with its padding and keep settings),
    // as POST LiveTv/Timers and LiveTv/SeriesTimers take it back.
    void timerDefaults(const QString& programId, QObject* context, Callback<QJsonObject> done);
    // POST LiveTv/Timers with `timer` (timerDefaults' answer).
    void createTimer(const QJsonObject& timer, QObject* context, Callback<Empty> done);
    // POST LiveTv/SeriesTimers with `timer` (timerDefaults' answer).
    void createSeriesTimer(const QJsonObject& timer, QObject* context, Callback<Empty> done);
    // timerDefaults, then createTimer: records one programme.
    void recordProgram(const QString& programId, QObject* context, Callback<Empty> done);
    // timerDefaults, then createSeriesTimer: records every episode of the
    // programme's series the server finds in the guide (on its channel,
    // new episodes, at any time, as the server's defaults say).
    void recordSeries(const QString& programId, QObject* context, Callback<Empty> done);
    // DELETE LiveTv/Timers/{id}: a recording to come is dropped, one in
    // progress stops (what it has recorded stays a recording).
    void cancelTimer(const QString& timerId, QObject* context, Callback<Empty> done);
    // DELETE LiveTv/SeriesTimers/{id}: its timers go with it.
    void cancelSeriesTimer(const QString& seriesTimerId, QObject* context, Callback<Empty> done);

    // `2026-11-10T16:00:00.000Z`, as the date filters take it.
    static QString queryDate(const QDateTime& when);

  private:
    static void allChannelPages(JellyfinClient& client, LiveTvChannelQuery query,
                                QList<LiveTvChannel> got, QObject* context,
                                Callback<QList<LiveTvChannel>> done);
    static void allProgramPages(JellyfinClient& client, LiveTvProgramQuery query,
                                QList<LiveTvProgram> got, QObject* context,
                                Callback<QList<LiveTvProgram>> done);

    JellyfinClient& m_client;
  };

} // namespace jfd::api
