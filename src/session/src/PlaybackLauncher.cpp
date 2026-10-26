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
    // What was queued behind the old file goes with it.
    ++m_preloadSerial;
    m_client.item(request.itemId, this, [this, serial, request](api::Result<api::Item> item) {
      if (serial != m_serial) return;
      if (!item) {
        Q_EMIT failed(request.itemId, item.error().message);
        return;
      }
      openWithItem(serial, request, *item);
    });
  }

  void PlaybackLauncher::preloadNext(const Request& request) {
    const std::uint64_t serial = ++m_preloadSerial;
    m_client.item(request.itemId, this, [this, serial, request](api::Result<api::Item> item) {
      if (serial != m_preloadSerial) return;
      if (!item) {
        Q_EMIT failed(request.itemId, item.error().message);
        return;
      }
      openWithItem(serial, request, *item, true, true);
    });
  }

  PlaybackLauncher::Request PlaybackLauncher::withChosenTracks(const Request& request,
                                                               const api::Item& item,
                                                               const api::MediaSource& source) const {
    if (!m_chooseTracks) return request;
    Request chosen = request;
    m_chooseTracks(item, source, chosen);
    if (request.audioStreamIndex) chosen.audioStreamIndex = request.audioStreamIndex;
    if (request.subtitleStreamIndex) chosen.subtitleStreamIndex = request.subtitleStreamIndex;
    // Only the tracks are the chooser's to change.
    Request result = request;
    result.audioStreamIndex = chosen.audioStreamIndex;
    result.subtitleStreamIndex = chosen.subtitleStreamIndex;
    return result;
  }

  void PlaybackLauncher::openWithItem(std::uint64_t serial, const Request& request,
                                      const api::Item& item, bool chosen, bool preload) {
    api::PlaybackInfoRequest info;
    info.itemId = request.itemId;
    info.mediaSourceId = request.mediaSourceId;
    if (request.startTicks > 0) info.startTimeTicks = request.startTicks;
    info.audioStreamIndex = request.audioStreamIndex;
    info.subtitleStreamIndex = request.subtitleStreamIndex;
    info.deviceProfile = m_deviceProfile;
    m_client.playbackInfo(info, this, [this, serial, request, item, chosen, preload](api::Result<api::PlaybackInfo> result) {
      if (serial != (preload ? m_preloadSerial : m_serial)) return;
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
      if (!chosen && m_chooseTracks) {
        Request withTracks = withChosenTracks(request, item, *source);
        const bool changed = withTracks.audioStreamIndex != request.audioStreamIndex ||
                             withTracks.subtitleStreamIndex != request.subtitleStreamIndex;
        // A transcode carries its tracks in the stream: ask the server again
        // with them. A direct stream plays all tracks; the player selects.
        if (changed && stream->method == api::PlayMethod::Transcode) {
          withTracks.mediaSourceId = source->id;
          openWithItem(serial, withTracks, item, true, preload);
          return;
        }
        openResolved(withTracks, item, *source, *stream, preload);
        return;
      }
      openResolved(request, item, *source, *stream, preload);
    });
  }

  void PlaybackLauncher::openResolved(const Request& request, const api::Item& item,
                                      const api::MediaSource& source, const api::PlaybackStream& stream,
                                      bool preload) {
    const auto start = core::MediaTicks::fromTicks(request.startTicks).value_or(core::MediaTicks{});
    std::optional<core::MediaTicks> runTime;
    if (source.runTimeTicks) runTime = core::MediaTicks::fromTicks(*source.runTimeTicks);
    core::MediaRequest media = playback::mediaRequest(
        stream, item.mediaType == QLatin1String("Audio"), start, runTime, request.startPaused);
    media.playlistItem = request.playlistItem;
    media.title = titleOf(item).toStdString();
    if (preload) {
      Q_EMIT preloaded(request, item, source, stream);
      m_post(core::intent::PreloadNext{std::move(media)});
      return;
    }
    Q_EMIT resolved(request, item, source, stream);
    m_post(core::intent::Open{std::move(media)});
  }

} // namespace jfd::session
