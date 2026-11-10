#include <jfd/api/LiveTv.h>

#include <jfd/api/ServerTime.h>

#include <QJsonArray>
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

} // namespace jfd::api
