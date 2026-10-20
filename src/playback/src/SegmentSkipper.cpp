#include <jfd/playback/SegmentSkipper.h>

#include <QStringList>

#include <algorithm>
#include <utility>

namespace jfd::playback {

  namespace {
    // Seeking to the very end of a file ends it; stop just short so the
    // player stays on the last frame and next-episode logic can run.
    constexpr std::int64_t EndMarginTicks = 1'000'000; // 100 ms

    bool skippable(const QString& type) {
      static const QStringList types{QStringLiteral("Intro"), QStringLiteral("Outro"),
                                     QStringLiteral("Recap"), QStringLiteral("Preview"),
                                     QStringLiteral("Commercial")};
      return types.contains(type, Qt::CaseInsensitive);
    }
  } // namespace

  SegmentSkipper::SegmentSkipper(std::function<void(core::SessionInput)> post)
      : m_post(std::move(post)) {}

  void SegmentSkipper::begin(core::MediaEpoch epoch, const QList<api::MediaSegment>& segments) {
    m_epoch = epoch;
    m_segments.clear();
    for (const api::MediaSegment& segment : segments)
      if (skippable(segment.type) && segment.endTicks > segment.startTicks)
        m_segments.append(segment);
    std::sort(m_segments.begin(), m_segments.end(),
              [](const auto& a, const auto& b) { return a.startTicks < b.startTicks; });
    m_duration.reset();
    m_current.reset();
    m_autoSkipped.clear();
  }

  std::optional<api::MediaSegment> SegmentSkipper::current() const {
    if (!m_current) return std::nullopt;
    return m_segments.at(*m_current);
  }

  void SegmentSkipper::skip() {
    if (m_current) skipTo(*m_current);
  }

  std::optional<int> SegmentSkipper::indexAt(std::int64_t ticks) const {
    for (int i = 0; i < m_segments.size(); ++i)
      if (ticks >= m_segments[i].startTicks && ticks < m_segments[i].endTicks) return i;
    return std::nullopt;
  }

  void SegmentSkipper::skipTo(int index) {
    std::int64_t target = m_segments.at(index).endTicks;
    if (m_duration) target = std::min(target, m_duration->ticks() - EndMarginTicks);
    m_current.reset();
    if (const auto position = core::MediaTicks::fromTicks(std::max<std::int64_t>(target, 0)))
      m_post(core::intent::Seek{*position});
  }

  void SegmentSkipper::snapshotChanged(const core::SessionSnapshot& snapshot) {
    if (!m_epoch || snapshot.media != *m_epoch) return;
    m_duration = snapshot.duration;
    const bool playing = snapshot.phase == core::PlaybackPhase::Playing ||
                         snapshot.phase == core::PlaybackPhase::Paused;
    m_current = playing ? indexAt(snapshot.position.ticks()) : std::nullopt;
    if (!m_current || !m_autoSkip || snapshot.phase != core::PlaybackPhase::Playing) return;
    if (!m_autoSkipped.insert(*m_current).second) return;
    skipTo(*m_current);
  }

} // namespace jfd::playback
