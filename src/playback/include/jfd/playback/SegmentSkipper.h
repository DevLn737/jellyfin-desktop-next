#pragma once

#include <jfd/api/Models.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/SessionSnapshot.h>

#include <QList>
#include <QString>

#include <functional>
#include <optional>
#include <set>

// Intros, recaps, outros and the like of the playing item (MediaSegments),
// and skipping them: the player shows a skip button while one plays, and
// with auto-skip on seeks past it once. Skipping is an intent::Seek, so in a
// SyncPlay group it seeks the whole group.
namespace jfd::playback {

  class SegmentSkipper {
  public:
    explicit SegmentSkipper(std::function<void(core::SessionInput)> post);

    // Off by default: a skip the viewer did not ask for is a surprise.
    void setAutoSkip(bool on) { m_autoSkip = on; }
    bool autoSkip() const { return m_autoSkip; }

    // The segments of the file the session plays as `epoch`. Types other
    // than Intro, Outro, Recap, Preview and Commercial are ignored.
    void begin(core::MediaEpoch epoch, const QList<api::MediaSegment>& segments);

    // The segment playing now, for the skip button; empty when none.
    std::optional<api::MediaSegment> current() const;

    // Seeks past the current segment. Does nothing outside one.
    void skip();

    void snapshotChanged(const core::SessionSnapshot& snapshot);

  private:
    std::optional<int> indexAt(std::int64_t ticks) const;
    void skipTo(int index);

    std::function<void(core::SessionInput)> m_post;
    bool m_autoSkip = false;
    std::optional<core::MediaEpoch> m_epoch;
    QList<api::MediaSegment> m_segments;
    std::optional<core::MediaTicks> m_duration;
    std::optional<int> m_current;
    // Segments auto-skipped once already: seeking back into one plays it.
    std::set<int> m_autoSkipped;
  };

} // namespace jfd::playback
