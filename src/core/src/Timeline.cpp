#include <jfd/core/Timeline.h>

#include <algorithm>
#include <cmath>

namespace jfd::core {

  Timeline::Timeline(StreamTimeline stream, MediaTicks requestedStart)
      : m_stream(stream), m_requestedStart(requestedStart),
        m_ready(stream.kind == StreamTimeline::Kind::Relative) {}

  bool Timeline::configure(std::optional<double> demuxerStartSeconds) {
    if (m_stream.kind == StreamTimeline::Kind::Relative) return true;
    if (!demuxerStartSeconds || !std::isfinite(*demuxerStartSeconds) ||
        std::fabs(*demuxerStartSeconds) > 1e9)
      return false;
    m_sourceStart = *demuxerStartSeconds;
    m_ready = true;
    return true;
  }

  std::optional<BackendSeconds> Timeline::startPosition() const {
    // A zero-start live stream begins at its first packet, even with nonzero PTS.
    if (!m_ready || m_requestedStart.ticks() == 0) return std::nullopt;
    return toBackend(m_requestedStart);
  }

  std::optional<BackendSeconds> Timeline::toBackend(MediaTicks position) const {
    if (!m_ready) return std::nullopt;
    switch (m_stream.kind) {
    case StreamTimeline::Kind::Relative:
      if (position < m_stream.offset) return std::nullopt;
      return BackendSeconds{static_cast<double>(position.ticks() - m_stream.offset.ticks()) / 1e7};
    case StreamTimeline::Kind::PreservedVideo:
      return BackendSeconds{position.seconds()};
    case StreamTimeline::Kind::PreservedMusic:
      return BackendSeconds{std::max(0.0, position.seconds() - m_sourceStart)};
    }
    return std::nullopt;
  }

  std::optional<MediaTicks> Timeline::toMedia(BackendSeconds position) const {
    if (!m_ready || !std::isfinite(position.value) || position.value < 0.0) return std::nullopt;
    switch (m_stream.kind) {
    case StreamTimeline::Kind::Relative: {
      const auto relative = MediaTicks::fromSeconds(position.value);
      if (!relative) return std::nullopt;
      return MediaTicks::fromTicks(relative->ticks() + m_stream.offset.ticks());
    }
    case StreamTimeline::Kind::PreservedVideo:
      return MediaTicks::fromSeconds(position.value);
    case StreamTimeline::Kind::PreservedMusic:
      return MediaTicks::fromSeconds(std::max(0.0, position.value + m_sourceStart));
    }
    return std::nullopt;
  }

  std::optional<MediaTicks> Timeline::duration(double backendDurationSeconds) const {
    if (!m_ready || !std::isfinite(backendDurationSeconds) || backendDurationSeconds < 0.0)
      return std::nullopt;
    switch (m_stream.kind) {
    case StreamTimeline::Kind::Relative: {
      const auto relative = MediaTicks::fromSeconds(backendDurationSeconds);
      if (!relative) return std::nullopt;
      return MediaTicks::fromTicks(relative->ticks() + m_stream.offset.ticks());
    }
    case StreamTimeline::Kind::PreservedVideo:
    case StreamTimeline::Kind::PreservedMusic:
      return MediaTicks::fromSeconds(std::max(0.0, backendDurationSeconds + m_sourceStart));
    }
    return std::nullopt;
  }

} // namespace jfd::core
