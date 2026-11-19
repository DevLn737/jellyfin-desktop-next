#include <jfd/playback/TrackController.h>

#include <algorithm>
#include <utility>

namespace jfd::playback {

  TrackController::TrackController(Callbacks callbacks) : m_callbacks(std::move(callbacks)) {}

  void TrackController::begin(core::MediaEpoch epoch, const api::MediaSource& source,
                              const api::PlaybackStream& stream,
                              std::optional<int> audioStreamIndex,
                              std::optional<int> subtitleStreamIndex) {
    m_epoch = epoch;
    m_table = TrackTable(source, stream);
    m_pending.clear();
    m_added = 0;
    m_reported.reset();
    m_loading = {false, false};
    run(m_table.selectInitial(audioStreamIndex, subtitleStreamIndex));
  }

  void TrackController::select(TrackKind kind, std::optional<int> streamIndex) {
    if (!m_epoch) return;
    run(m_table.select(kind, streamIndex));
  }

  void TrackController::run(std::vector<Command> commands) {
    for (Command& command : commands) {
      if (auto* select = std::get_if<command::Select>(&command)) {
        m_callbacks.post(core::intent::SelectTrack{*m_epoch, select->kind, select->trackId});
      } else if (auto* add = std::get_if<command::Add>(&command)) {
        m_pending.push_back({add->key, m_added++, add->track.url});
        m_callbacks.post(core::intent::AddExternalTrack{*m_epoch, std::move(add->track)});
      } else if (auto* reopen = std::get_if<command::Reopen>(&command)) {
        if (m_callbacks.reopen) m_callbacks.reopen(*reopen);
      }
    }
    const Loading loading{m_table.loading(TrackKind::Audio), m_table.loading(TrackKind::Subtitle)};
    if (loading != m_loading) {
      m_loading = loading;
      if (m_callbacks.loadingChanged) m_callbacks.loadingChanged();
    }
    const Selection now{m_table.selected(TrackKind::Audio), m_table.selected(TrackKind::Subtitle)};
    if (now == m_reported) return;
    m_reported = now;
    if (m_callbacks.selectionChanged) m_callbacks.selectionChanged(now.first, now.second);
  }

  void TrackController::snapshotChanged(const core::SessionSnapshot& snapshot) {
    if (!m_epoch || snapshot.media != *m_epoch) return;
    using Status = core::ExternalTrackState::Status;
    // Answers can add commands (and pending adds), so collect them first.
    std::vector<std::pair<std::uint64_t, std::optional<int>>> answers;
    std::erase_if(m_pending, [&](const PendingAdd& add) {
      if (add.position >= snapshot.externalTracks.size()) return false;
      const core::ExternalTrackState& state = snapshot.externalTracks[add.position];
      if (state.track.url != add.url || state.status == Status::Adding) return false;
      answers.emplace_back(add.key, state.status == Status::Added ? state.trackId : std::nullopt);
      return true;
    });
    for (const auto& [key, trackId] : answers) {
      if (trackId) {
        run(m_table.trackAdded(key, *trackId));
        continue;
      }
      const auto kind = m_table.pendingKind(key);
      const auto wanted = kind ? m_table.selected(*kind) : std::nullopt;
      run(m_table.trackFailed(key));
      // Given up on the track the viewer wanted: it is gone from the menu.
      if (kind && wanted && m_table.failed(*wanted) && m_callbacks.loadFailed) m_callbacks.loadFailed(*kind);
    }
  }

} // namespace jfd::playback
