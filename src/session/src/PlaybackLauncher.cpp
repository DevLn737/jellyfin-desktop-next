#include <jfd/session/PlaybackLauncher.h>

#include <jfd/api/JellyfinClient.h>
#include <jfd/playback/StreamPlan.h>

#include <QTimer>
#include <QPointer>

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

  std::optional<LocalCopy> PlaybackLauncher::localCopyFor(const Request& request) const {
    if (!m_local) return std::nullopt;
    std::optional<LocalCopy> copy = m_local->find(request.itemId);
    if (!copy) return std::nullopt;
    if (!request.mediaSourceId.isEmpty() && request.mediaSourceId != copy->source.id)
      return std::nullopt;
    if (!request.playlistItem.isEmpty() && !copy->original) return std::nullopt;
    return copy;
  }

  void PlaybackLauncher::openLocal(std::uint64_t serial, const Request& request, const LocalCopy& copy,
                                   bool preload) {
    api::PlaybackStream stream;
    stream.url = QUrl::fromLocalFile(copy.path);
    stream.method = api::PlayMethod::DirectPlay;
    stream.itemId = request.itemId;
    stream.mediaSourceId = copy.source.id;
    stream.externalSubtitles = copy.externalSubtitles;
    // A file plays all its tracks: the player selects, nothing is reopened.
    const Request withTracks = preload ? request : withChosenTracks(request, copy.item, copy.source);
    openResolved(serial, withTracks, copy.item, copy.source, stream, preload);
  }

  void PlaybackLauncher::open(const Request& request) {
    const std::uint64_t serial = ++m_serial;
    // What was queued behind the old file goes with it.
    ++m_preloadSerial;
    if (std::optional<LocalCopy> copy = localCopyFor(request)) {
      // Later, as an answer from the server would be: callers set up their
      // state after open() returns.
      QTimer::singleShot(0, this, [this, serial, request, copy = std::move(*copy)] {
        if (serial == m_serial) openLocal(serial, request, copy, false);
      });
      return;
    }
    m_client.item(request.itemId, this, [this, serial, request](api::Result<api::Item> item) {
      if (serial != m_serial) return;
      if (!item) {
        fail(request, item.error().message, false);
        return;
      }
      openWithItem(serial, request, *item);
    });
  }

  void PlaybackLauncher::preloadNext(const Request& request) {
    const std::uint64_t serial = ++m_preloadSerial;
    if (std::optional<LocalCopy> copy = localCopyFor(request)) {
      QTimer::singleShot(0, this, [this, serial, request, copy = std::move(*copy)] {
        if (serial == m_preloadSerial) openLocal(serial, request, copy, true);
      });
      return;
    }
    m_client.item(request.itemId, this, [this, serial, request](api::Result<api::Item> item) {
      if (serial != m_preloadSerial) return;
      if (!item) {
        fail(request, item.error().message, true);
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
        fail(request, result.error().message, preload);
        return;
      }
      if (!result->errorCode.isEmpty() || result->mediaSources.isEmpty()) {
        fail(request,
             result->errorCode.isEmpty() ? QStringLiteral("No playable source.") : result->errorCode,
             preload);
        return;
      }
      const api::MediaSource* source = &result->mediaSources.front();
      for (const auto& candidate : result->mediaSources)
        if (!request.mediaSourceId.isEmpty() && candidate.id == request.mediaSourceId) source = &candidate;
      const auto stream =
          api::resolvePlaybackStream(m_client.streamContext(), request.itemId, *result, *source);
      if (!stream) {
        fail(request, stream.error().message, preload);
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
        openResolved(serial, withTracks, item, *source, *stream, preload);
        return;
      }
      openResolved(serial, request, item, *source, *stream, preload);
    });
  }

  void PlaybackLauncher::fail(const Request& request, const QString& message, bool preload) {
    Q_EMIT failed(request.itemId, message);
    if (!preload) Q_EMIT openFailed(request, message);
  }

  void PlaybackLauncher::openResolved(std::uint64_t serial, const Request& request, const api::Item& item,
                                      const api::MediaSource& source, const api::PlaybackStream& stream,
                                      bool preload) {
    const auto start = core::MediaTicks::fromTicks(request.startTicks).value_or(core::MediaTicks{});
    std::optional<core::MediaTicks> runTime;
    // A live channel has no end, whatever length the tuner guessed.
    if (source.runTimeTicks && !stream.isLive)
      runTime = core::MediaTicks::fromTicks(*source.runTimeTicks);
    core::MediaRequest media = playback::mediaRequest(
        stream, item.mediaType == QLatin1String("Audio"), start, runTime, request.startPaused);
    media.playlistItem = request.playlistItem;
    media.title = titleOf(item).toStdString();
    if (preload) {
      Q_EMIT preloaded(request, item, source, stream);
      m_post(core::intent::PreloadNext{std::move(media)});
      return;
    }
    auto open = [this, serial, request, item, source, stream, media = std::move(media)]() mutable {
      if (serial != m_serial) return;
      Q_EMIT resolved(request, item, source, stream);
      m_post(core::intent::Open{std::move(media)});
    };
    if (!m_openGate) {
      open();
      return;
    }
    // The gate may hold the open past this launcher's lifetime: the
    // continuation checks that the launcher still exists.
    m_openGate(request, item, source, [guard = QPointer<PlaybackLauncher>(this), open = std::move(open)]() mutable {
      if (guard) open();
    });
  }

} // namespace jfd::session
