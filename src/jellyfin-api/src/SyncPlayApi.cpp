#include <jfd/api/SyncPlayApi.h>

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace jfd::api {

  namespace {
    using Method = JellyfinClient::Method;

    QJsonDocument body(const QJsonObject& object) {
      return QJsonDocument(object);
    }

    QJsonObject stateBody(const SyncPlayReadyState& state) {
      return QJsonObject{
          {QStringLiteral("When"), formatServerInstant(state.when)},
          {QStringLiteral("PositionTicks"), static_cast<double>(state.positionTicks)},
          {QStringLiteral("IsPlaying"), state.isPlaying},
          {QStringLiteral("PlaylistItemId"), state.playlistItemId}};
    }

    ApiError unsupportedRoute(const ApiError& original) {
      ApiError error = original;
      error.kind = ApiError::Kind::Unsupported;
      error.problemCode = QStringLiteral("SyncPlayPlaybackRateUnavailable");
      error.message = QStringLiteral("The server has no SyncPlay playback-rate extension");
      return error;
    }
  } // namespace

  void SyncPlayApi::createGroup(const QString& groupName, QObject* context,
                                Callback<SyncPlayGroup> done) {
    m_client.sendJson(Method::Post, QStringLiteral("SyncPlay/New"), {},
                      body({{QStringLiteral("GroupName"), groupName}}), context,
                      [done = std::move(done)](Result<QJsonObject> result) {
                        if (!result)
                          done(result.error());
                        else
                          done(json::syncPlayGroup(result.value()));
                      });
  }

  void SyncPlayApi::joinGroup(const QString& groupId, QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Join"), {},
                           body({{QStringLiteral("GroupId"), groupId}}), context, std::move(done));
  }

  void SyncPlayApi::leaveGroup(QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Leave"), {}, std::nullopt,
                           context, std::move(done));
  }

  void SyncPlayApi::listGroups(QObject* context, Callback<QList<SyncPlayGroup>> done) {
    m_client.send(Method::Get, QStringLiteral("SyncPlay/List"), {}, std::nullopt, context,
                  [done = std::move(done)](Result<JellyfinClient::RawResponse> result) {
                    if (!result) {
                      done(result.error());
                      return;
                    }
                    const auto document = QJsonDocument::fromJson(result->body);
                    if (!document.isArray()) {
                      ApiError error;
                      error.kind = ApiError::Kind::InvalidResponse;
                      error.message = QStringLiteral("SyncPlay/List is not an array");
                      done(error);
                      return;
                    }
                    QList<SyncPlayGroup> groups;
                    for (const auto& value : document.array()) {
                      auto group = json::syncPlayGroup(value.toObject());
                      if (!group) {
                        done(group.error());
                        return;
                      }
                      groups.append(std::move(group).value());
                    }
                    done(std::move(groups));
                  });
  }

  void SyncPlayApi::group(const QString& groupId, QObject* context, Callback<SyncPlayGroup> done) {
    m_client.sendJson(Method::Get, QStringLiteral("SyncPlay/") + groupId, {}, std::nullopt, context,
                      [done = std::move(done)](Result<QJsonObject> result) {
                        if (!result)
                          done(result.error());
                        else
                          done(json::syncPlayGroup(result.value()));
                      });
  }

  void SyncPlayApi::setNewQueue(const QStringList& itemIds, int playingItemPosition,
                                std::int64_t startPositionTicks, QObject* context,
                                Callback<Empty> done) {
    m_client.sendNoContent(
        Method::Post, QStringLiteral("SyncPlay/SetNewQueue"), {},
        body({{QStringLiteral("PlayingQueue"), QJsonArray::fromStringList(itemIds)},
              {QStringLiteral("PlayingItemPosition"), playingItemPosition},
              {QStringLiteral("StartPositionTicks"), static_cast<double>(startPositionTicks)}}),
        context, std::move(done));
  }

  void SyncPlayApi::setPlaylistItem(const QString& playlistItemId, QObject* context,
                                    Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/SetPlaylistItem"), {},
                           body({{QStringLiteral("PlaylistItemId"), playlistItemId}}), context,
                           std::move(done));
  }

  void SyncPlayApi::unpause(QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Unpause"), {}, std::nullopt,
                           context, std::move(done));
  }

  void SyncPlayApi::pause(QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Pause"), {}, std::nullopt,
                           context, std::move(done));
  }

  void SyncPlayApi::stop(QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Stop"), {}, std::nullopt, context,
                           std::move(done));
  }

  void SyncPlayApi::seek(std::int64_t positionTicks, QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(
        Method::Post, QStringLiteral("SyncPlay/Seek"), {},
        body({{QStringLiteral("PositionTicks"), static_cast<double>(positionTicks)}}), context,
        std::move(done));
  }

  void SyncPlayApi::setPlaybackRate(double rate, QObject* context, Callback<Empty> done) {
    if (!std::isfinite(rate)) {
      ApiError error;
      error.kind = ApiError::Kind::InvalidResponse;
      error.message = QStringLiteral("Playback rate must be finite");
      done(error);
      return;
    }
    m_client.sendNoContent(
        Method::Post, QStringLiteral("SyncPlay/SetPlaybackRate"), {},
        body({{QStringLiteral("PlaybackRate"), rate}}), context,
        [done = std::move(done)](Result<Empty> result) {
          if (!result && result.error().kind == ApiError::Kind::Http &&
              (result.error().httpStatus == 404 || result.error().httpStatus == 405)) {
            done(unsupportedRoute(result.error()));
            return;
          }
          done(std::move(result));
        });
  }

  void SyncPlayApi::ready(const SyncPlayReadyState& state, QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Ready"), {},
                           body(stateBody(state)), context, std::move(done));
  }

  void SyncPlayApi::buffering(const SyncPlayReadyState& state, QObject* context,
                              Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Buffering"), {},
                           body(stateBody(state)), context, std::move(done));
  }

  void SyncPlayApi::setIgnoreWait(bool ignoreWait, QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/SetIgnoreWait"), {},
                           body({{QStringLiteral("IgnoreWait"), ignoreWait}}), context,
                           std::move(done));
  }

  void SyncPlayApi::ping(std::int64_t pingMs, QObject* context, Callback<Empty> done) {
    m_client.sendNoContent(Method::Post, QStringLiteral("SyncPlay/Ping"), {},
                           body({{QStringLiteral("Ping"), static_cast<double>(pingMs)}}), context,
                           std::move(done));
  }

  void SyncPlayApi::utcTime(const core::Clock& clock, QObject* context,
                            Callback<UtcTimeSample> done) {
    const core::LocalInstant sent = clock.now();
    m_client.sendJson(Method::Get, QStringLiteral("GetUtcTime"), {}, std::nullopt, context,
                      [&clock, sent, done = std::move(done)](Result<QJsonObject> result) {
                        const core::LocalInstant received = clock.now();
                        if (!result) {
                          done(result.error());
                          return;
                        }
                        const auto reception = parseServerInstant(
                            result->value(QLatin1String("RequestReceptionTime")).toString());
                        const auto transmission = parseServerInstant(
                            result->value(QLatin1String("ResponseTransmissionTime")).toString());
                        if (!reception || !transmission) {
                          ApiError error;
                          error.kind = ApiError::Kind::InvalidResponse;
                          error.message = QStringLiteral("GetUtcTime has no valid timestamps");
                          done(error);
                          return;
                        }
                        done(UtcTimeSample{sent, *reception, *transmission, received});
                      });
  }

} // namespace jfd::api
