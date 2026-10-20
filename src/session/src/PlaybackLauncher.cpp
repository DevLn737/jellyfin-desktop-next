#include <jfd/session/PlaybackLauncher.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/playback/StreamPlan.h>

#include <utility>

namespace jfd::session {

  PlaybackLauncher::PlaybackLauncher(api::JellyfinClient& client, QJsonObject deviceProfile,
                                     Post post, QObject* parent)
      : QObject(parent), m_client(client), m_deviceProfile(std::move(deviceProfile)),
        m_post(std::move(post)) {}

  QString PlaybackLauncher::titleOf(const api::Item& item) {
    if (item.seriesName.isEmpty()) return item.name;
    return item.seriesName + QStringLiteral(" – ") + item.name;
  }

  void PlaybackLauncher::open(const Request& request) {
    const std::uint64_t serial = ++m_serial;
    m_client.item(request.itemId, this, [this, serial, request](api::Result<api::Item> item) {
      if (serial != m_serial) return;
      if (!item) {
        Q_EMIT failed(request.itemId, item.error().message);
        return;
      }
      openWithItem(serial, request, *item);
    });
  }

  void PlaybackLauncher::openWithItem(std::uint64_t serial, const Request& request,
                                      const api::Item& item) {
    api::PlaybackInfoRequest info;
    info.itemId = request.itemId;
    info.mediaSourceId = request.mediaSourceId;
    if (request.startTicks > 0) info.startTimeTicks = request.startTicks;
    info.audioStreamIndex = request.audioStreamIndex;
    info.subtitleStreamIndex = request.subtitleStreamIndex;
    info.deviceProfile = m_deviceProfile;
    m_client.playbackInfo(info, this, [this, serial, request, item](api::Result<api::PlaybackInfo> result) {
      if (serial != m_serial) return;
      if (!result) {
        Q_EMIT failed(request.itemId, result.error().message);
        return;
      }
      if (!result->errorCode.isEmpty() || result->mediaSources.isEmpty()) {
        Q_EMIT failed(request.itemId, result->errorCode.isEmpty() ? QStringLiteral("No playable source.")
                                                                  : result->errorCode);
        return;
      }
      const api::MediaSource* source = &result->mediaSources.front();
      for (const auto& candidate : result->mediaSources)
        if (!request.mediaSourceId.isEmpty() && candidate.id == request.mediaSourceId) source = &candidate;
      const auto stream =
          api::resolvePlaybackStream(m_client.streamContext(), request.itemId, *result, *source);
      if (!stream) {
        Q_EMIT failed(request.itemId, stream.error().message);
        return;
      }

      const auto start = core::MediaTicks::fromTicks(request.startTicks).value_or(core::MediaTicks{});
      std::optional<core::MediaTicks> runTime;
      if (source->runTimeTicks) runTime = core::MediaTicks::fromTicks(*source->runTimeTicks);
      core::MediaRequest media = playback::mediaRequest(
          *stream, item.mediaType == QLatin1String("Audio"), start, runTime, request.startPaused);
      media.playlistItem = request.playlistItem;
      media.title = titleOf(item).toStdString();
      Q_EMIT resolved(request, item, *source, *stream);
      m_post(core::intent::Open{std::move(media)});
    });
  }

} // namespace jfd::session
