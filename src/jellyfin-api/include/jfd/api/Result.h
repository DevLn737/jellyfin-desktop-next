#pragma once

#include <QJsonObject>
#include <QString>

#include <utility>
#include <variant>

// Every request completes with either a value or an ApiError. Errors carry
// enough detail for the caller to tell a lost connection from a rejected
// request, and a SyncPlay 409 from any other conflict.
namespace jfd::api {

  struct ApiError {
    enum class Kind {
      // The server could not be reached, or the connection dropped.
      Network,
      Timeout,
      // The request was cancelled because its owner went away.
      Canceled,
      // The server answered with a status outside 2xx.
      Http,
      // The server answered 2xx with a body that does not match the API.
      InvalidResponse,
      // The server does not offer this feature (for example a stock server
      // asked for the SyncPlay playback-rate extension).
      Unsupported,
    };

    Kind kind = Kind::Network;
    int httpStatus = 0;
    QString message;
    // ProblemDetails "code" extension, for example
    // "SyncPlayPlaybackRateNotSupported".
    QString problemCode;
    QJsonObject problem;

    bool isUnauthorized() const { return kind == Kind::Http && httpStatus == 401; }
    bool isForbidden() const { return kind == Kind::Http && httpStatus == 403; }
    bool isNotFound() const { return kind == Kind::Http && httpStatus == 404; }
    // A SyncPlay group refused a non-1x rate, or refused this client joining
    // a group that plays at a non-1x rate, because a participant lacks the
    // playback-rate capability.
    bool isPlaybackRateConflict() const {
      return kind == Kind::Http && httpStatus == 409 &&
             problemCode == QLatin1String("SyncPlayPlaybackRateNotSupported");
    }
    // Number of participants without playback-rate support, from a
    // playback-rate conflict; -1 when the server did not say.
    int unsupportedParticipants() const {
      const auto value = problem.value(QLatin1String("unsupportedParticipants"));
      return value.isDouble() ? value.toInt() : -1;
    }
  };

  struct Empty {};

  template <class T> class Result {
  public:
    Result(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(ApiError error) : m_data(std::in_place_index<1>, std::move(error)) {}

    bool ok() const { return m_data.index() == 0; }
    explicit operator bool() const { return ok(); }

    const T& value() const& { return std::get<0>(m_data); }
    T& value() & { return std::get<0>(m_data); }
    T&& value() && { return std::get<0>(std::move(m_data)); }
    const T* operator->() const { return &std::get<0>(m_data); }
    const T& operator*() const { return std::get<0>(m_data); }

    const ApiError& error() const { return std::get<1>(m_data); }

  private:
    std::variant<T, ApiError> m_data;
  };

  // A request that returns no data.
  using Status = Result<Empty>;

} // namespace jfd::api
