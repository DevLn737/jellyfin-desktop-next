#include <jfd/core/Readiness.h>

#ifndef JFD_CORE_MUTATION
#define JFD_CORE_MUTATION 0
#endif

namespace jfd::core {

  void Readiness::begin(RequestId request, std::optional<MediaTicks> target,
                        bool expectSeekEvent) {
    m_request = request;
    m_target = target;
    m_awaitingSeekEvent = expectSeekEvent;
  }

  void Readiness::seekStarted() { m_awaitingSeekEvent = false; }

  void Readiness::failed(RequestId request) {
    if (m_request != request) return;
    m_request.reset();
    m_target.reset();
    m_awaitingSeekEvent = false;
  }

  std::optional<RequestId> Readiness::restarted(std::optional<MediaTicks> position, bool seeking,
                                                bool paused) {
#if JFD_CORE_MUTATION == 2
    // Mutation: any restart completes the latest request, at any position.
    (void)paused;
    if (!m_request || !position || seeking) return std::nullopt;
    const auto stale = m_request;
    m_request.reset();
    m_target.reset();
    return stale;
#endif
    // A restart queued before the latest seek began belongs to an earlier request.
    if (!m_request || m_awaitingSeekEvent || !position || seeking) return std::nullopt;
    // An unpaused file may have moved past the target before the sample was
    // read, but never back before it: a restart there is an earlier keyframe.
    if (m_target) {
      const MediaDelta offset = *position - *m_target;
      if (paused ? offset.abs() > ReadyTolerance : offset < -ReadyTolerance) return std::nullopt;
    }
    const auto request = m_request;
    m_request.reset();
    m_target.reset();
    return request;
  }

} // namespace jfd::core
