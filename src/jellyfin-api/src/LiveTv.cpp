#include <jfd/api/LiveTv.h>

#include <jfd/api/ServerTime.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace jfd::api {

  namespace {
    ApiError invalid(const QString& what) {
      ApiError error;
      error.kind = ApiError::Kind::InvalidResponse;
      error.message = what;
      return error;
    }

    QString text(const QJsonObject& o, const char* key) {
      return o.value(QLatin1String(key)).toString();
    }

    bool flag(const QJsonObject& o, const char* key) {
      const QJsonValue v = o.value(QLatin1String(key));
      return v.isBool() && v.toBool();
    }

    int count(const QJsonObject& o, const char* key, int fallback) {
      const QJsonValue v = o.value(QLatin1String(key));
      if (!v.isDouble()) return fallback;
      const double d = v.toDouble();
      if (!std::isfinite(d) || d < 0 || d > 2147483647.0) return fallback;
      return static_cast<int>(d);
    }

    template <class T, class Parse> Callback<QJsonObject> parsed(Callback<T> done, Parse parse) {
      return [done = std::move(done), parse](Result<QJsonObject> result) {
        if (!result) return done(result.error());
        auto value = parse(result.value());
        if (!value) return done(value.error());
        done(std::move(value).value());
      };
    }

    qint64 ticks(const QJsonObject& o, const char* key) {
      const QJsonValue v = o.value(QLatin1String(key));
      if (!v.isDouble()) return 0;
      const double d = v.toDouble();
      if (!std::isfinite(d) || d < 0 || d > 9.0e18) return 0;
      return static_cast<qint64>(d);
    }

    QString escaped(const QString& id) { return QString::fromLatin1(QUrl::toPercentEncoding(id)); }

    const QString True = QStringLiteral("true");
    const QString False = QStringLiteral("false");
  } // namespace

  bool LiveTvInfo::availableTo(const QString& userId) const {
    if (!isEnabled) return false;
    const bool working = std::any_of(services.cbegin(), services.cend(), [](const LiveTvService& s) {
      return s.status.compare(QLatin1String("Ok"), Qt::CaseInsensitive) == 0;
    });
    if (!working) return false;
    if (enabledUsers.isEmpty()) return true;
    // The server writes ids without dashes in some places and with them in
    // others; compare both ways.
    QString plain = userId;
    plain.remove(QLatin1Char('-'));
    return std::any_of(enabledUsers.cbegin(), enabledUsers.cend(), [&](QString id) {
      id.remove(QLatin1Char('-'));
      return id.compare(plain, Qt::CaseInsensitive) == 0;
    });
  }

  bool LiveTvProgram::isOnAt(const QDateTime& now) const {
    return isValid() && start <= now && now < end;
  }

  bool LiveTvTimer::isScheduled() const {
    return status.isEmpty() || status == QLatin1String("New") ||
           status == QLatin1String("InProgress") || status == QLatin1String("ConflictedOk");
  }

  double LiveTvProgram::progressAt(const QDateTime& now) const {
    if (!isValid()) return 0.0;
    const double length = static_cast<double>(start.msecsTo(end));
    const double done = static_cast<double>(start.msecsTo(now));
    return std::clamp(done / length, 0.0, 1.0);
  }

  namespace json {

    QDateTime serverDate(const QJsonValue& value) {
      if (!value.isString()) return {};
      const QString s = value.toString();
      std::optional<core::ServerInstant> instant = parseServerInstant(s);
      if (!instant) {
        // The guide's dates sometimes come without a zone; the server
        // stores them in UTC [unverified on 12.2: seen on older servers].
        instant = parseServerInstant(QString(s + QLatin1Char('Z')));
        if (!instant) return {};
      }
      const qint64 ms = instant->sinceOrigin().count() / 10'000;
      // Qt 6.4 and 6.11 both take this form without a deprecation.
      return QDateTime::fromMSecsSinceEpoch(ms).toUTC();
    }

    Result<LiveTvInfo> liveTvInfo(const QJsonObject& o) {
      LiveTvInfo info;
      info.isEnabled = flag(o, "IsEnabled");
      for (const QJsonValue& value : o.value(QLatin1String("Services")).toArray()) {
        const QJsonObject s = value.toObject();
        LiveTvService service;
        service.name = text(s, "Name");
        service.status = text(s, "Status");
        service.statusMessage = text(s, "StatusMessage");
        const QJsonValue visible = s.value(QLatin1String("IsVisible"));
        service.isVisible = !visible.isBool() || visible.toBool();
        info.services.append(std::move(service));
      }
      for (const QJsonValue& value : o.value(QLatin1String("EnabledUsers")).toArray())
        if (value.isString()) info.enabledUsers.append(value.toString());
      return info;
    }

    Result<LiveTvProgram> liveTvProgram(const QJsonObject& o) {
      LiveTvProgram program;
      program.id = text(o, "Id");
      if (program.id.isEmpty()) return invalid(QStringLiteral("Programme has no Id"));
      program.channelId = text(o, "ChannelId");
      program.name = text(o, "Name");
      program.episodeTitle = text(o, "EpisodeTitle");
      program.overview = text(o, "Overview");
      program.start = serverDate(o.value(QLatin1String("StartDate")));
      program.end = serverDate(o.value(QLatin1String("EndDate")));
      program.isLive = flag(o, "IsLive");
      program.isNews = flag(o, "IsNews");
      program.isMovie = flag(o, "IsMovie");
      program.isSeries = flag(o, "IsSeries");
      program.timerId = text(o, "TimerId");
      program.seriesTimerId = text(o, "SeriesTimerId");
      return program;
    }

    Result<LiveTvChannel> liveTvChannel(const QJsonObject& o) {
      LiveTvChannel channel;
      channel.id = text(o, "Id");
      if (channel.id.isEmpty()) return invalid(QStringLiteral("Channel has no Id"));
      channel.name = text(o, "Name");
      channel.number = text(o, "ChannelNumber");
      if (channel.number.isEmpty()) channel.number = text(o, "Number");
      channel.primaryImageTag =
          o.value(QLatin1String("ImageTags")).toObject().value(QLatin1String("Primary")).toString();
      channel.isFavorite = o.value(QLatin1String("UserData"))
                               .toObject()
                               .value(QLatin1String("IsFavorite"))
                               .toBool();
      const QJsonValue current = o.value(QLatin1String("CurrentProgram"));
      if (current.isObject()) {
        auto program = liveTvProgram(current.toObject());
        // A programme without dates says nothing about now; it is dropped,
        // not the channel.
        if (program && program->isValid()) {
          channel.currentProgram = std::move(program).value();
          if (channel.currentProgram->channelId.isEmpty())
            channel.currentProgram->channelId = channel.id;
        }
      }
      return channel;
    }

    Result<LiveTvChannelPage> liveTvChannelPage(const QJsonObject& o) {
      const QJsonValue items = o.value(QLatin1String("Items"));
      if (!items.isArray()) return invalid(QStringLiteral("Channel list has no Items"));
      LiveTvChannelPage page;
      for (const QJsonValue& value : items.toArray()) {
        auto channel = liveTvChannel(value.toObject());
        if (!channel) return channel.error();
        page.channels.append(std::move(channel).value());
      }
      page.totalRecordCount = count(o, "TotalRecordCount", static_cast<int>(page.channels.size()));
      page.startIndex = count(o, "StartIndex", 0);
      return page;
    }

    Result<LiveTvProgramPage> liveTvProgramPage(const QJsonObject& o) {
      const QJsonValue items = o.value(QLatin1String("Items"));
      if (!items.isArray()) return invalid(QStringLiteral("Programme list has no Items"));
      LiveTvProgramPage page;
      for (const QJsonValue& value : items.toArray()) {
        auto program = liveTvProgram(value.toObject());
        if (!program) return program.error();
        if (!program->isValid()) continue;
        page.programs.append(std::move(program).value());
      }
      page.totalRecordCount =
          count(o, "TotalRecordCount", static_cast<int>(items.toArray().size()));
      page.startIndex = count(o, "StartIndex", 0);
      page.itemCount = static_cast<int>(items.toArray().size());
      return page;
    }

    Result<LiveTvTimer> liveTvTimer(const QJsonObject& o) {
      LiveTvTimer timer;
      timer.id = text(o, "Id");
      if (timer.id.isEmpty()) return invalid(QStringLiteral("Timer has no Id"));
      timer.channelId = text(o, "ChannelId");
      timer.channelName = text(o, "ChannelName");
      timer.programId = text(o, "ProgramId");
      timer.name = text(o, "Name");
      timer.overview = text(o, "Overview");
      timer.start = serverDate(o.value(QLatin1String("StartDate")));
      timer.end = serverDate(o.value(QLatin1String("EndDate")));
      timer.status = text(o, "Status");
      timer.seriesTimerId = text(o, "SeriesTimerId");
      return timer;
    }

    Result<QList<LiveTvTimer>> liveTvTimerList(const QJsonObject& o) {
      const QJsonValue items = o.value(QLatin1String("Items"));
      if (!items.isArray()) return invalid(QStringLiteral("Timer list has no Items"));
      QList<LiveTvTimer> timers;
      for (const QJsonValue& value : items.toArray()) {
        auto timer = liveTvTimer(value.toObject());
        if (!timer) return timer.error();
        timers.append(std::move(timer).value());
      }
      return timers;
    }

    Result<LiveTvRecording> liveTvRecording(const QJsonObject& o) {
      LiveTvRecording r;
      r.id = text(o, "Id");
      if (r.id.isEmpty()) return invalid(QStringLiteral("Recording has no Id"));
      r.type = text(o, "Type");
      r.name = text(o, "Name");
      r.seriesName = text(o, "SeriesName");
      r.overview = text(o, "Overview");
      r.channelName = text(o, "ChannelName");
      r.status = text(o, "Status");
      r.timerId = text(o, "TimerId");
      r.start = serverDate(o.value(QLatin1String("StartDate")));
      r.end = serverDate(o.value(QLatin1String("EndDate")));
      r.dateCreated = serverDate(o.value(QLatin1String("DateCreated")));
      r.runTimeTicks = ticks(o, "RunTimeTicks");
      const QJsonObject userData = o.value(QLatin1String("UserData")).toObject();
      r.playbackPositionTicks = ticks(userData, "PlaybackPositionTicks");
      r.played = flag(userData, "Played");
      r.primaryImageTag =
          o.value(QLatin1String("ImageTags")).toObject().value(QLatin1String("Primary")).toString();
      return r;
    }

    Result<LiveTvRecordingPage> liveTvRecordingPage(const QJsonObject& o) {
      const QJsonValue items = o.value(QLatin1String("Items"));
      if (!items.isArray()) return invalid(QStringLiteral("Recording list has no Items"));
      LiveTvRecordingPage page;
      for (const QJsonValue& value : items.toArray()) {
        auto recording = liveTvRecording(value.toObject());
        if (!recording) return recording.error();
        page.recordings.append(std::move(recording).value());
      }
      page.totalRecordCount =
          count(o, "TotalRecordCount", static_cast<int>(page.recordings.size()));
      page.startIndex = count(o, "StartIndex", 0);
      return page;
    }

  } // namespace json

  QString LiveTvApi::queryDate(const QDateTime& when) {
    return when.toUTC().toString(Qt::ISODateWithMs);
  }

  void LiveTvApi::info(QObject* context, Callback<LiveTvInfo> done) {
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("LiveTv/Info"), {}, std::nullopt,
                      context, parsed<LiveTvInfo>(std::move(done), json::liveTvInfo));
  }

  void LiveTvApi::channels(const LiveTvChannelQuery& q, QObject* context,
                           Callback<LiveTvChannelPage> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    query.addQueryItem(QStringLiteral("startIndex"), QString::number(q.startIndex));
    query.addQueryItem(QStringLiteral("limit"), QString::number(q.limit));
    query.addQueryItem(QStringLiteral("addCurrentProgram"), q.addCurrentProgram ? True : False);
    query.addQueryItem(QStringLiteral("enableImages"), True);
    query.addQueryItem(QStringLiteral("enableUserData"), True);
    query.addQueryItem(QStringLiteral("enableTotalRecordCount"), True);
    if (q.isFavorite) query.addQueryItem(QStringLiteral("isFavorite"), *q.isFavorite ? True : False);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("LiveTv/Channels"), query,
                      std::nullopt, context,
                      parsed<LiveTvChannelPage>(std::move(done), json::liveTvChannelPage));
  }

  void LiveTvApi::allChannels(const LiveTvChannelQuery& query, QObject* context,
                              Callback<QList<LiveTvChannel>> done) {
    allChannelPages(m_client, query, {}, context, std::move(done));
  }

  void LiveTvApi::allChannelPages(JellyfinClient& client, LiveTvChannelQuery query,
                                  QList<LiveTvChannel> got, QObject* context,
                                  Callback<QList<LiveTvChannel>> done) {
    JellyfinClient* c = &client;
    LiveTvApi(client).channels(
        query, context,
        [c, query, got = std::move(got), context, done = std::move(done)](
            Result<LiveTvChannelPage> page) mutable {
          if (!page) return done(page.error());
          got.append(page->channels);
          const int next = page->startIndex + static_cast<int>(page->channels.size());
          if (page->channels.isEmpty() || next >= page->totalRecordCount)
            return done(std::move(got));
          query.startIndex = next;
          allChannelPages(*c, query, std::move(got), context, std::move(done));
        });
  }

  void LiveTvApi::channel(const QString& channelId, QObject* context, Callback<LiveTvChannel> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    m_client.sendJson(JellyfinClient::Method::Get,
                      QStringLiteral("LiveTv/Channels/") + escaped(channelId),
                      query, std::nullopt, context,
                      parsed<LiveTvChannel>(std::move(done), json::liveTvChannel));
  }

  void LiveTvApi::allPrograms(const LiveTvProgramQuery& query, QObject* context,
                              Callback<QList<LiveTvProgram>> done) {
    allProgramPages(m_client, query, {}, context, std::move(done));
  }

  void LiveTvApi::allProgramPages(JellyfinClient& client, LiveTvProgramQuery query,
                                  QList<LiveTvProgram> got, QObject* context,
                                  Callback<QList<LiveTvProgram>> done) {
    JellyfinClient* c = &client;
    LiveTvApi(client).programs(
        query, context,
        [c, query, got = std::move(got), context, done = std::move(done)](
            Result<LiveTvProgramPage> page) mutable {
          if (!page) return done(page.error());
          // A page of only undated programmes still moves on to the next.
          const int read = page->itemCount;
          got.append(page->programs);
          // Pages count every programme, dated or not.
          const int next = page->startIndex + query.limit;
          if (read == 0 || query.limit <= 0 || next >= page->totalRecordCount) {
            std::stable_sort(got.begin(), got.end(), [](const LiveTvProgram& a, const LiveTvProgram& b) {
              return a.start < b.start;
            });
            return done(std::move(got));
          }
          query.startIndex = next;
          allProgramPages(*c, query, std::move(got), context, std::move(done));
        });
  }

  void LiveTvApi::programs(const LiveTvProgramQuery& q, QObject* context,
                           Callback<LiveTvProgramPage> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    if (!q.channelIds.isEmpty())
      query.addQueryItem(QStringLiteral("channelIds"), q.channelIds.join(QLatin1Char(',')));
    if (q.from.isValid()) query.addQueryItem(QStringLiteral("minEndDate"), queryDate(q.from));
    if (q.to.isValid()) query.addQueryItem(QStringLiteral("maxStartDate"), queryDate(q.to));
    query.addQueryItem(QStringLiteral("sortBy"), QStringLiteral("StartDate"));
    query.addQueryItem(QStringLiteral("startIndex"), QString::number(q.startIndex));
    query.addQueryItem(QStringLiteral("limit"), QString::number(q.limit));
    query.addQueryItem(QStringLiteral("enableImages"), False);
    query.addQueryItem(QStringLiteral("enableUserData"), False);
    query.addQueryItem(QStringLiteral("enableTotalRecordCount"), True);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("LiveTv/Programs"), query,
                      std::nullopt, context,
                      parsed<LiveTvProgramPage>(std::move(done), json::liveTvProgramPage));
  }

  void LiveTvApi::closeLiveStream(const QString& liveStreamId, QObject* context,
                                  Callback<Empty> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("liveStreamId"), liveStreamId);
    m_client.sendNoContent(JellyfinClient::Method::Post, QStringLiteral("LiveStreams/Close"), query,
                           std::nullopt, context, std::move(done));
  }

  void LiveTvApi::recordings(const LiveTvRecordingQuery& q, QObject* context,
                             Callback<LiveTvRecordingPage> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("userId"), m_client.userId());
    query.addQueryItem(QStringLiteral("startIndex"), QString::number(q.startIndex));
    query.addQueryItem(QStringLiteral("limit"), QString::number(q.limit));
    if (q.isInProgress)
      query.addQueryItem(QStringLiteral("isInProgress"), *q.isInProgress ? True : False);
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("DateCreated,Overview"));
    query.addQueryItem(QStringLiteral("enableImages"), True);
    query.addQueryItem(QStringLiteral("imageTypeLimit"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("enableImageTypes"), QStringLiteral("Primary"));
    query.addQueryItem(QStringLiteral("enableUserData"), True);
    query.addQueryItem(QStringLiteral("enableTotalRecordCount"), True);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("LiveTv/Recordings"), query,
                      std::nullopt, context,
                      parsed<LiveTvRecordingPage>(std::move(done), json::liveTvRecordingPage));
  }

  void LiveTvApi::deleteRecording(const QString& recordingId, QObject* context,
                                  Callback<Empty> done) {
    m_client.sendNoContent(JellyfinClient::Method::Delete,
                           QStringLiteral("Items/") + escaped(recordingId), {},
                           std::nullopt, context, std::move(done));
  }

  void LiveTvApi::timers(const LiveTvTimerQuery& q, QObject* context,
                         Callback<QList<LiveTvTimer>> done) {
    QUrlQuery query;
    if (!q.channelId.isEmpty()) query.addQueryItem(QStringLiteral("channelId"), q.channelId);
    if (!q.seriesTimerId.isEmpty())
      query.addQueryItem(QStringLiteral("seriesTimerId"), q.seriesTimerId);
    if (q.isActive) query.addQueryItem(QStringLiteral("isActive"), *q.isActive ? True : False);
    if (q.isScheduled)
      query.addQueryItem(QStringLiteral("isScheduled"), *q.isScheduled ? True : False);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("LiveTv/Timers"), query,
                      std::nullopt, context,
                      parsed<QList<LiveTvTimer>>(std::move(done), json::liveTvTimerList));
  }

  void LiveTvApi::seriesTimers(QObject* context, Callback<QList<LiveTvTimer>> done) {
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("LiveTv/SeriesTimers"), {},
                      std::nullopt, context,
                      parsed<QList<LiveTvTimer>>(std::move(done), json::liveTvTimerList));
  }

  void LiveTvApi::timerDefaults(const QString& programId, QObject* context,
                                Callback<QJsonObject> done) {
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("programId"), programId);
    m_client.sendJson(JellyfinClient::Method::Get, QStringLiteral("LiveTv/Timers/Defaults"), query,
                      std::nullopt, context, std::move(done));
  }

  void LiveTvApi::createTimer(const QJsonObject& timer, QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(JellyfinClient::Method::Post, QStringLiteral("LiveTv/Timers"), {},
                           QJsonDocument(timer), context, std::move(done));
  }

  void LiveTvApi::createSeriesTimer(const QJsonObject& timer, QObject* context,
                                    Callback<Empty> done) {
    m_client.sendNoContent(JellyfinClient::Method::Post, QStringLiteral("LiveTv/SeriesTimers"), {},
                           QJsonDocument(timer), context, std::move(done));
  }

  void LiveTvApi::recordProgram(const QString& programId, QObject* context, Callback<Empty> done) {
    JellyfinClient* c = &m_client;
    timerDefaults(programId, context,
                  [c, context, done = std::move(done)](Result<QJsonObject> defaults) mutable {
                    if (!defaults) return done(defaults.error());
                    LiveTvApi(*c).createTimer(*defaults, context, std::move(done));
                  });
  }

  void LiveTvApi::recordSeries(const QString& programId, QObject* context, Callback<Empty> done) {
    JellyfinClient* c = &m_client;
    timerDefaults(programId, context,
                  [c, context, done = std::move(done)](Result<QJsonObject> defaults) mutable {
                    if (!defaults) return done(defaults.error());
                    LiveTvApi(*c).createSeriesTimer(*defaults, context, std::move(done));
                  });
  }

  void LiveTvApi::cancelTimer(const QString& timerId, QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(JellyfinClient::Method::Delete,
                           QStringLiteral("LiveTv/Timers/") + escaped(timerId), {}, std::nullopt,
                           context, std::move(done));
  }

  void LiveTvApi::cancelSeriesTimer(const QString& seriesTimerId, QObject* context,
                                    Callback<Empty> done) {
    m_client.sendNoContent(JellyfinClient::Method::Delete,
                           QStringLiteral("LiveTv/SeriesTimers/") + escaped(seriesTimerId), {},
                           std::nullopt, context, std::move(done));
  }

} // namespace jfd::api
