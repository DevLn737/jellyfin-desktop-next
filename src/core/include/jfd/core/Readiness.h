#pragma once

#include <jfd/core/Epoch.h>
#include <jfd/core/Time.h>

#include <optional>

// Decides when a load or seek is ready, for one file. Ported from
// PlaybackReadiness in the 2.x client:
// - only a restart reported after the latest load or seek counts;
// - a seek or a new request invalidates any restart seen before it;
// - readiness needs a fresh position and the backend no longer seeking;
// - while paused, the position must be within 500 ms of the seek target, so an
//   earlier restart is never relabelled as readiness for the latest request.
namespace jfd::core {

  inline constexpr MediaDelta ReadyTolerance = MediaDelta::fromMilliseconds(500);

  class Readiness {
  public:
    // A load or seek was issued; earlier restarts no longer count. A seek is
    // only complete after the backend reported that it started seeking; the
    // initial start of a file need not report one.
    void begin(RequestId request, std::optional<MediaTicks> target, bool expectSeekEvent);
    // The backend started seeking (MPV_EVENT_SEEK).
    void seekStarted();
    // The latest request failed; it will never become ready.
    void failed(RequestId request);
    // A restart with a fresh sample. Returns the request it completes, if any.
    std::optional<RequestId> restarted(std::optional<MediaTicks> position, bool seeking,
                                       bool paused);

    std::optional<RequestId> pending() const { return m_request; }

  private:
    std::optional<RequestId> m_request;
    std::optional<MediaTicks> m_target;
    bool m_awaitingSeekEvent = false;
  };

} // namespace jfd::core
