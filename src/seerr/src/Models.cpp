#include <jfd/seerr/Models.h>

#include <QJsonArray>

namespace jfd::seerr {

  int Title::year() const {
    return releaseDate.size() >= 4 ? releaseDate.left(4).toInt() : 0;
  }

  bool Title::canRequest() const {
    switch (status) {
    case MediaStatus::Pending:
    case MediaStatus::Processing:
    case MediaStatus::Available:
      return false;
    default:
      return true;
    }
  }

  namespace json {

    namespace {
      QString str(const QJsonObject& o, const char* key) {
        return o.value(QLatin1String(key)).toString();
      }
      int integer(const QJsonObject& o, const char* key) {
        return o.value(QLatin1String(key)).toInt();
      }
    } // namespace

    MediaStatus mediaStatus(int value) {
      return value >= 1 && value <= 6 ? static_cast<MediaStatus>(value) : MediaStatus::None;
    }

    RequestStatus requestStatus(int value) {
      return value >= 1 && value <= 3 ? static_cast<RequestStatus>(value) : RequestStatus::None;
    }

    std::optional<Title> title(const QJsonObject& o, std::optional<MediaType> fallback) {
      const QString kind = str(o, "mediaType");
      MediaType type;
      if (kind == QLatin1String("movie")) type = MediaType::Movie;
      else if (kind == QLatin1String("tv")) type = MediaType::Tv;
      else if (kind.isEmpty() && fallback) type = *fallback;
      else return std::nullopt;
      if (!o.value(QLatin1String("id")).isDouble()) return std::nullopt;

      Title t;
      t.type = type;
      t.tmdbId = integer(o, "id");
      t.title = type == MediaType::Movie ? str(o, "title") : str(o, "name");
      t.overview = str(o, "overview");
      t.posterPath = str(o, "posterPath");
      t.backdropPath = str(o, "backdropPath");
      t.releaseDate = type == MediaType::Movie ? str(o, "releaseDate") : str(o, "firstAirDate");
      t.voteAverage = o.value(QLatin1String("voteAverage")).toDouble();
      t.status = mediaStatus(o.value(QLatin1String("mediaInfo")).toObject().value(QLatin1String("status")).toInt());
      if (t.tmdbId <= 0 || t.title.isEmpty()) return std::nullopt;
      return t;
    }

    TitlePage titlePage(const QJsonObject& o) {
      TitlePage page;
      page.page = o.value(QLatin1String("page")).toInt(1);
      page.totalPages = o.value(QLatin1String("totalPages")).toInt(1);
      page.totalResults = integer(o, "totalResults");
      for (const QJsonValue& value : o.value(QLatin1String("results")).toArray()) {
        if (auto t = title(value.toObject())) page.results.append(*t);
      }
      return page;
    }

    UserRequest userRequest(const QJsonObject& o) {
      UserRequest r;
      r.id = integer(o, "id");
      r.status = requestStatus(integer(o, "status"));
      const QJsonObject media = o.value(QLatin1String("media")).toObject();
      r.type = str(media, "mediaType") == QLatin1String("tv") ? MediaType::Tv : MediaType::Movie;
      // Some answers carry "type" on the request itself instead.
      if (str(o, "type") == QLatin1String("tv")) r.type = MediaType::Tv;
      r.tmdbId = integer(media, "tmdbId");
      r.mediaStatus = mediaStatus(integer(media, "status"));
      r.requestedBy = o.value(QLatin1String("requestedBy")).toObject().value(QLatin1String("id")).toInt();
      for (const QJsonValue& season : o.value(QLatin1String("seasons")).toArray())
        r.seasons.append(season.toObject().value(QLatin1String("seasonNumber")).toInt());
      return r;
    }

    RequestPage requestPage(const QJsonObject& o) {
      RequestPage page;
      page.total = o.value(QLatin1String("pageInfo")).toObject().value(QLatin1String("results")).toInt();
      for (const QJsonValue& value : o.value(QLatin1String("results")).toArray()) {
        const UserRequest r = userRequest(value.toObject());
        if (r.id > 0) page.results.append(r);
      }
      if (page.total < page.results.size()) page.total = static_cast<int>(page.results.size());
      return page;
    }

    TitleDetails details(const QJsonObject& o, MediaType type) {
      TitleDetails d;
      QJsonObject withKind = o;
      withKind.insert(QLatin1String("mediaType"),
                      type == MediaType::Movie ? QLatin1String("movie") : QLatin1String("tv"));
      if (auto t = title(withKind)) d.title = *t;
      else d.title.type = type;
      d.tagline = str(o, "tagline");
      if (type == MediaType::Movie) {
        d.runtimeMinutes = integer(o, "runtime");
      } else {
        const QJsonArray runtimes = o.value(QLatin1String("episodeRunTime")).toArray();
        if (!runtimes.isEmpty()) d.runtimeMinutes = runtimes.first().toInt();
      }
      for (const QJsonValue& genre : o.value(QLatin1String("genres")).toArray())
        d.genres.append(genre.toObject().value(QLatin1String("name")).toString());

      const QJsonObject info = o.value(QLatin1String("mediaInfo")).toObject();
      QHash<int, MediaStatus> seasonStatus;
      for (const QJsonValue& value : info.value(QLatin1String("seasons")).toArray()) {
        const QJsonObject s = value.toObject();
        seasonStatus.insert(integer(s, "seasonNumber"), mediaStatus(integer(s, "status")));
      }
      for (const QJsonValue& value : o.value(QLatin1String("seasons")).toArray()) {
        const QJsonObject s = value.toObject();
        Season season;
        season.number = integer(s, "seasonNumber");
        if (season.number <= 0) continue;
        season.episodeCount = integer(s, "episodeCount");
        season.name = str(s, "name");
        season.status = seasonStatus.value(season.number, MediaStatus::None);
        d.seasons.append(season);
      }
      for (const QJsonValue& value : info.value(QLatin1String("requests")).toArray()) {
        UserRequest r = userRequest(value.toObject());
        if (r.tmdbId == 0) r.tmdbId = d.title.tmdbId;
        if (r.id > 0) d.requests.append(r);
      }
      return d;
    }

    User user(const QJsonObject& o) {
      User u;
      u.id = integer(o, "id");
      for (const char* key : {"displayName", "jellyfinUsername", "username", "email"}) {
        u.name = str(o, key);
        if (!u.name.isEmpty()) break;
      }
      u.permissions = integer(o, "permissions");
      u.requestCount = integer(o, "requestCount");
      return u;
    }

    std::optional<ServerInfo> serverInfo(const QJsonObject& status, const QJsonObject& settings) {
      if (!status.value(QLatin1String("version")).isString()) return std::nullopt;
      if (!settings.value(QLatin1String("initialized")).isBool()) return std::nullopt;
      ServerInfo info;
      info.version = str(status, "version");
      info.initialized = settings.value(QLatin1String("initialized")).toBool();
      info.mediaServerType = integer(settings, "mediaServerType");
      return info;
    }

  } // namespace json
} // namespace jfd::seerr
