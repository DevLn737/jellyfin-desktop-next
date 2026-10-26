#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// What Seerr says about titles, requests and the signed-in user, parsed from
// its JSON (docs/seerr.md). Fields the client does not show are dropped.
namespace jfd::seerr {

  enum class MediaType { Movie, Tv };

  // Availability of a title (mediaInfo.status). None: Seerr has no record of
  // it, so it was never requested.
  enum class MediaStatus {
    None = 0,
    Unknown = 1,
    Pending = 2,
    Processing = 3,
    PartiallyAvailable = 4,
    Available = 5,
    Deleted = 6,
  };

  enum class RequestStatus {
    None = 0,
    PendingApproval = 1,
    Approved = 2,
    Declined = 3,
  };

  struct Title {
    MediaType type = MediaType::Movie;
    int tmdbId = 0;
    QString title;
    QString overview;
    // TMDB image paths ("/abc.jpg"); the client asks Seerr's image proxy.
    QString posterPath;
    QString backdropPath;
    // "2024-05-17" or empty.
    QString releaseDate;
    double voteAverage = 0;
    MediaStatus status = MediaStatus::None;

    int year() const;
    // A title that is available or has a request in progress cannot be
    // requested again.
    bool canRequest() const;
    bool operator==(const Title&) const = default;
  };

  struct TitlePage {
    int page = 1;
    int totalPages = 1;
    int totalResults = 0;
    QList<Title> results;
  };

  struct Season {
    int number = 0;
    int episodeCount = 0;
    QString name;
    MediaStatus status = MediaStatus::None;
    bool operator==(const Season&) const = default;
  };

  struct UserRequest {
    int id = 0;
    RequestStatus status = RequestStatus::None;
    MediaType type = MediaType::Movie;
    int tmdbId = 0;
    int requestedBy = 0;
    MediaStatus mediaStatus = MediaStatus::None;
    QList<int> seasons;
    bool operator==(const UserRequest&) const = default;
  };

  struct TitleDetails {
    Title title;
    QString tagline;
    int runtimeMinutes = 0;
    QStringList genres;
    // Series only; season 0 (specials) is left out.
    QList<Season> seasons;
    QList<UserRequest> requests;
  };

  struct RequestPage {
    int total = 0;
    QList<UserRequest> results;
  };

  struct User {
    int id = 0;
    QString name;
    int permissions = 0;
    int requestCount = 0;
  };

  struct ServerInfo {
    QString version;
    bool initialized = false;
    // 1 Plex, 2 Jellyfin, 3 Emby, 4 none set up.
    int mediaServerType = 0;
    bool usesJellyfin() const { return initialized && mediaServerType == 2; }
  };

  namespace json {
    MediaStatus mediaStatus(int value);
    RequestStatus requestStatus(int value);
    // Movies and series; people and anything else are dropped (nullopt).
    std::optional<Title> title(const QJsonObject& object, std::optional<MediaType> fallback = {});
    TitlePage titlePage(const QJsonObject& object);
    UserRequest userRequest(const QJsonObject& object);
    RequestPage requestPage(const QJsonObject& object);
    TitleDetails details(const QJsonObject& object, MediaType type);
    User user(const QJsonObject& object);
    std::optional<ServerInfo> serverInfo(const QJsonObject& status, const QJsonObject& publicSettings);
  }

} // namespace jfd::seerr
