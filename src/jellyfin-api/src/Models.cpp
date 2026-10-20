#include <jfd/api/Models.h>

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace jfd::api::json {

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

    bool flag(const QJsonObject& o, const char* key, bool fallback = false) {
      const auto v = o.value(QLatin1String(key));
      return v.isBool() ? v.toBool() : fallback;
    }

    // JSON numbers are doubles; ticks fit exactly up to 2^53 (about 28 years
    // of media), and anything that is not a whole number is rejected.
    std::optional<std::int64_t> integer(const QJsonObject& o, const char* key) {
      const auto v = o.value(QLatin1String(key));
      if (!v.isDouble()) return std::nullopt;
      const double d = v.toDouble();
      if (!std::isfinite(d) || std::trunc(d) != d || std::fabs(d) > 9007199254740992.0)
        return std::nullopt;
      return static_cast<std::int64_t>(d);
    }

    std::optional<int> smallInteger(const QJsonObject& o, const char* key) {
      const auto v = integer(o, key);
      if (!v || *v < INT32_MIN || *v > INT32_MAX) return std::nullopt;
      return static_cast<int>(*v);
    }

    std::optional<double> number(const QJsonObject& o, const char* key) {
      const auto v = o.value(QLatin1String(key));
      if (!v.isDouble() || !std::isfinite(v.toDouble())) return std::nullopt;
      return v.toDouble();
    }

    QStringList strings(const QJsonObject& o, const char* key) {
      QStringList out;
      for (const auto& value : o.value(QLatin1String(key)).toArray())
        if (value.isString()) out.append(value.toString());
      return out;
    }

    void readUserData(const QJsonObject& o, UserItemData& d) {
      d.itemId = text(o, "ItemId");
      d.playbackPositionTicks = integer(o, "PlaybackPositionTicks").value_or(0);
      d.played = flag(o, "Played");
      d.playCount = smallInteger(o, "PlayCount").value_or(0);
      d.isFavorite = flag(o, "IsFavorite");
      d.unplayedItemCount = smallInteger(o, "UnplayedItemCount");
      d.playedPercentage = number(o, "PlayedPercentage");
    }

    // {"<media source id>": {"<width>": TrickplayInfoDto}}; resolutions are
    // sorted by width so callers can pick the smallest that fits.
    QHash<QString, QList<TrickplayInfo>> trickplay(const QJsonObject& o) {
      QHash<QString, QList<TrickplayInfo>> out;
      for (auto source = o.begin(); source != o.end(); ++source) {
        QList<TrickplayInfo> resolutions;
        const QJsonObject byWidth = source.value().toObject();
        for (auto entry = byWidth.begin(); entry != byWidth.end(); ++entry) {
          const QJsonObject t = entry.value().toObject();
          TrickplayInfo info;
          info.width = smallInteger(t, "Width").value_or(0);
          info.height = smallInteger(t, "Height").value_or(0);
          info.tileWidth = smallInteger(t, "TileWidth").value_or(0);
          info.tileHeight = smallInteger(t, "TileHeight").value_or(0);
          info.thumbnailCount = smallInteger(t, "ThumbnailCount").value_or(0);
          info.intervalMs = smallInteger(t, "Interval").value_or(0);
          info.bandwidth = smallInteger(t, "Bandwidth").value_or(0);
          if (info.width > 0 && info.height > 0 && info.thumbnailsPerTile() > 0 &&
              info.thumbnailCount > 0 && info.intervalMs > 0)
            resolutions.append(info);
        }
        std::sort(resolutions.begin(), resolutions.end(),
                  [](const TrickplayInfo& a, const TrickplayInfo& b) { return a.width < b.width; });
        if (!resolutions.isEmpty()) out.insert(source.key(), resolutions);
      }
      return out;
    }

    QString segmentType(const QJsonValue& v) {
      if (v.isString()) return v.toString();
      // Older servers without string enums.
      static const char* const Names[] = {"Unknown", "Commercial", "Preview",
                                          "Recap",   "Outro",      "Intro"};
      const int n = v.toInt(-1);
      if (n >= 0 && n < 6) return QString::fromLatin1(Names[n]);
      return QStringLiteral("Unknown");
    }
  } // namespace

  Result<PublicServerInfo> publicServerInfo(const QJsonObject& o) {
    PublicServerInfo info;
    info.id = text(o, "Id");
    info.name = text(o, "ServerName");
    info.versionText = text(o, "Version");
    info.startupWizardCompleted = flag(o, "StartupWizardCompleted", true);
    const auto version = ServerVersion::parse(info.versionText);
    if (info.id.isEmpty() || !version)
      return invalid(QStringLiteral("System/Info/Public has no Id or Version"));
    info.version = *version;
    return info;
  }

  Result<UserInfo> user(const QJsonObject& o) {
    UserInfo u;
    u.id = text(o, "Id");
    u.name = text(o, "Name");
    u.serverId = text(o, "ServerId");
    u.hasPassword = flag(o, "HasPassword", true);
    u.syncPlayAccess = o.value(QLatin1String("Policy"))
                           .toObject()
                           .value(QLatin1String("SyncPlayAccess"))
                           .toString();
    if (u.id.isEmpty()) return invalid(QStringLiteral("User has no Id"));
    return u;
  }

  Result<AuthenticationResult> authenticationResult(const QJsonObject& o) {
    AuthenticationResult r;
    r.accessToken = text(o, "AccessToken");
    r.serverId = text(o, "ServerId");
    r.sessionId =
        o.value(QLatin1String("SessionInfo")).toObject().value(QLatin1String("Id")).toString();
    auto u = user(o.value(QLatin1String("User")).toObject());
    if (r.accessToken.isEmpty() || !u)
      return invalid(QStringLiteral("Authentication result has no AccessToken or User"));
    r.user = std::move(u).value();
    return r;
  }

  Result<QuickConnectState> quickConnectState(const QJsonObject& o) {
    QuickConnectState s;
    s.secret = text(o, "Secret");
    s.code = text(o, "Code");
    s.authenticated = flag(o, "Authenticated");
    if (s.secret.isEmpty()) return invalid(QStringLiteral("Quick Connect result has no Secret"));
    return s;
  }

  Result<Item> item(const QJsonObject& o) {
    Item i;
    i.id = text(o, "Id");
    i.name = text(o, "Name");
    i.type = text(o, "Type");
    i.mediaType = text(o, "MediaType");
    i.isFolder = flag(o, "IsFolder");
    i.seriesId = text(o, "SeriesId");
    i.seriesName = text(o, "SeriesName");
    i.seasonId = text(o, "SeasonId");
    i.seasonName = text(o, "SeasonName");
    i.indexNumber = smallInteger(o, "IndexNumber");
    i.parentIndexNumber = smallInteger(o, "ParentIndexNumber");
    i.productionYear = smallInteger(o, "ProductionYear");
    i.runTimeTicks = integer(o, "RunTimeTicks");
    i.overview = text(o, "Overview");
    i.genres = strings(o, "Genres");
    i.communityRating = number(o, "CommunityRating");
    i.officialRating = text(o, "OfficialRating");
    i.collectionType = text(o, "CollectionType");
    i.childCount = smallInteger(o, "ChildCount");
    i.status = text(o, "Status");
    i.primaryImageAspectRatio = number(o, "PrimaryImageAspectRatio");
    const QJsonObject tags = o.value(QLatin1String("ImageTags")).toObject();
    for (auto tag = tags.begin(); tag != tags.end(); ++tag)
      if (tag.value().isString()) i.imageTags.insert(tag.key(), tag.value().toString());
    i.primaryImageTag = i.imageTags.value(QStringLiteral("Primary"));
    i.backdropImageTags = strings(o, "BackdropImageTags");
    i.seriesPrimaryImageTag = text(o, "SeriesPrimaryImageTag");
    i.parentBackdropItemId = text(o, "ParentBackdropItemId");
    i.parentBackdropImageTags = strings(o, "ParentBackdropImageTags");
    i.parentThumbItemId = text(o, "ParentThumbItemId");
    i.parentThumbImageTag = text(o, "ParentThumbImageTag");
    i.trickplay = trickplay(o.value(QLatin1String("Trickplay")).toObject());
    readUserData(o.value(QLatin1String("UserData")).toObject(), i.userData);
    if (i.id.isEmpty()) return invalid(QStringLiteral("Item has no Id"));
    return i;
  }

  Result<ItemPage> itemPage(const QJsonObject& o) {
    ItemPage page;
    const auto items = o.value(QLatin1String("Items"));
    if (!items.isArray()) return invalid(QStringLiteral("Item list has no Items"));
    for (const auto& value : items.toArray()) {
      auto parsed = item(value.toObject());
      if (!parsed) return parsed.error();
      page.items.append(std::move(parsed).value());
    }
    page.totalRecordCount =
        smallInteger(o, "TotalRecordCount").value_or(static_cast<int>(page.items.size()));
    page.startIndex = smallInteger(o, "StartIndex").value_or(0);
    return page;
  }

  Result<ItemPage> itemArray(const QJsonArray& array) {
    ItemPage page;
    for (const auto& value : array) {
      auto parsed = item(value.toObject());
      if (!parsed) return parsed.error();
      page.items.append(std::move(parsed).value());
    }
    page.totalRecordCount = static_cast<int>(page.items.size());
    return page;
  }

  Result<UserItemData> userItemData(const QJsonObject& o) {
    UserItemData d;
    readUserData(o, d);
    return d;
  }

  Result<ItemFilters> itemFilters(const QJsonObject& o) {
    ItemFilters f;
    f.genres = strings(o, "Genres");
    f.officialRatings = strings(o, "OfficialRatings");
    for (const auto& value : o.value(QLatin1String("Years")).toArray())
      if (value.isDouble()) f.years.append(value.toInt());
    return f;
  }

  Result<QList<MediaSegment>> mediaSegments(const QJsonObject& o) {
    const auto items = o.value(QLatin1String("Items"));
    if (!items.isArray()) return invalid(QStringLiteral("Media segment list has no Items"));
    QList<MediaSegment> out;
    for (const auto& value : items.toArray()) {
      const QJsonObject s = value.toObject();
      MediaSegment segment;
      segment.id = text(s, "Id");
      segment.itemId = text(s, "ItemId");
      segment.type = segmentType(s.value(QLatin1String("Type")));
      const auto start = integer(s, "StartTicks");
      const auto end = integer(s, "EndTicks");
      if (!start || !end || *end <= *start) continue;
      segment.startTicks = *start;
      segment.endTicks = *end;
      out.append(segment);
    }
    std::sort(out.begin(), out.end(), [](const MediaSegment& a, const MediaSegment& b) {
      return a.startTicks < b.startTicks;
    });
    return out;
  }

  Result<MediaStream> mediaStream(const QJsonObject& o) {
    MediaStream s;
    const auto index = smallInteger(o, "Index");
    if (!index) return invalid(QStringLiteral("Media stream has no Index"));
    s.index = *index;
    s.type = text(o, "Type");
    s.codec = text(o, "Codec");
    s.language = text(o, "Language");
    s.title = text(o, "Title");
    s.displayTitle = text(o, "DisplayTitle");
    s.isDefault = flag(o, "IsDefault");
    s.isForced = flag(o, "IsForced");
    s.isExternal = flag(o, "IsExternal");
    s.deliveryMethod = text(o, "DeliveryMethod");
    s.deliveryUrl = text(o, "DeliveryUrl");
    s.isExternalUrl = flag(o, "IsExternalUrl");
    s.realFrameRate = number(o, "RealFrameRate");
    s.averageFrameRate = number(o, "AverageFrameRate");
    s.channels = smallInteger(o, "Channels");
    return s;
  }

  Result<MediaSource> mediaSource(const QJsonObject& o) {
    MediaSource m;
    m.id = text(o, "Id");
    m.protocol = text(o, "Protocol");
    m.path = text(o, "Path");
    m.container = text(o, "Container");
    m.eTag = text(o, "ETag");
    m.supportsDirectPlay = flag(o, "SupportsDirectPlay");
    m.supportsDirectStream = flag(o, "SupportsDirectStream");
    m.supportsTranscoding = flag(o, "SupportsTranscoding");
    m.transcodingUrl = text(o, "TranscodingUrl");
    m.transcodingSubProtocol = text(o, "TranscodingSubProtocol");
    m.runTimeTicks = integer(o, "RunTimeTicks");
    m.defaultAudioStreamIndex = smallInteger(o, "DefaultAudioStreamIndex");
    m.defaultSubtitleStreamIndex = smallInteger(o, "DefaultSubtitleStreamIndex");
    m.requiredHttpHeaders = o.value(QLatin1String("RequiredHttpHeaders")).toObject();
    if (m.id.isEmpty()) return invalid(QStringLiteral("Media source has no Id"));
    for (const auto& value : o.value(QLatin1String("MediaStreams")).toArray()) {
      auto stream = mediaStream(value.toObject());
      if (!stream) return stream.error();
      m.mediaStreams.append(std::move(stream).value());
    }
    return m;
  }

  Result<PlaybackInfo> playbackInfo(const QJsonObject& o) {
    PlaybackInfo info;
    info.playSessionId = text(o, "PlaySessionId");
    info.errorCode = text(o, "ErrorCode");
    for (const auto& value : o.value(QLatin1String("MediaSources")).toArray()) {
      auto source = mediaSource(value.toObject());
      if (!source) return source.error();
      info.mediaSources.append(std::move(source).value());
    }
    if (info.mediaSources.isEmpty() && info.errorCode.isEmpty())
      return invalid(QStringLiteral("PlaybackInfo has neither MediaSources nor ErrorCode"));
    return info;
  }

  Result<SyncPlayGroup> syncPlayGroup(const QJsonObject& o) {
    SyncPlayGroup g;
    g.groupId = text(o, "GroupId");
    g.groupName = text(o, "GroupName");
    g.state = text(o, "State");
    for (const auto& value : o.value(QLatin1String("Participants")).toArray())
      g.participants.append(value.toString());
    const auto updated = text(o, "LastUpdatedAt");
    if (!updated.isEmpty()) g.lastUpdatedAt = parseServerInstant(updated);
    if (g.groupId.isEmpty()) return invalid(QStringLiteral("SyncPlay group has no GroupId"));
    return g;
  }

} // namespace jfd::api::json

namespace jfd::api {

  std::optional<TrickplayFrame> trickplayFrame(const TrickplayInfo& info, std::int64_t positionMs) {
    const int perTile = info.thumbnailsPerTile();
    if (perTile <= 0 || info.thumbnailCount <= 0 || info.intervalMs <= 0) return std::nullopt;
    const std::int64_t clamped = std::max<std::int64_t>(positionMs, 0);
    const auto thumbnail = static_cast<int>(
        std::min<std::int64_t>(clamped / info.intervalMs, info.thumbnailCount - 1));
    const int inTile = thumbnail % perTile;
    return TrickplayFrame{thumbnail / perTile, (inTile % info.tileWidth) * info.width,
                          (inTile / info.tileWidth) * info.height, info.width, info.height};
  }

} // namespace jfd::api
