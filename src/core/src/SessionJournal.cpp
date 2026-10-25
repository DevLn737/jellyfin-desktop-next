#include <jfd/core/SessionJournal.h>

#include <cinttypes>
#include <cstdio>
#include <utility>

namespace jfd::core {

  namespace {
    template <class... Ts> struct Overloaded : Ts... {
      using Ts::operator()...;
    };
    template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

    std::string seconds(MediaTicks position) {
      char text[32];
      std::snprintf(text, sizeof text, "%.3fs", static_cast<double>(position.milliseconds()) / 1000.0);
      return text;
    }

    std::string seconds(const std::optional<BackendSeconds>& position) {
      if (!position) return "none";
      char text[32];
      std::snprintf(text, sizeof text, "%.3fs", position->value);
      return text;
    }

    // Server time of day, UTC: the server log uses the same clock.
    std::string clock(ServerInstant when) {
      const std::int64_t ms = when.sinceOrigin().count() / 10'000;
      const std::int64_t day = ((ms % 86'400'000) + 86'400'000) % 86'400'000;
      char text[32];
      std::snprintf(text, sizeof text, "%02" PRId64 ":%02" PRId64 ":%02" PRId64 ".%03" PRId64,
                    day / 3'600'000, day / 60'000 % 60, day / 1000 % 60, day % 1000);
      return text;
    }

    std::string rate(PlaybackRate value) {
      char text[32];
      std::snprintf(text, sizeof text, "%.3gx", value.value());
      return text;
    }

    template <class Tag> std::string epoch(const char* name, Epoch<Tag> value) {
      return std::string(name) + "#" + std::to_string(value.value());
    }

    const char* name(GroupCommandType type) {
      switch (type) {
      case GroupCommandType::Unpause: return "Unpause";
      case GroupCommandType::Pause: return "Pause";
      case GroupCommandType::Stop: return "Stop";
      case GroupCommandType::Seek: return "Seek";
      }
      return "?";
    }

    const char* name(GroupState state) {
      switch (state) {
      case GroupState::Idle: return "Idle";
      case GroupState::Waiting: return "Waiting";
      case GroupState::Paused: return "Paused";
      case GroupState::Playing: return "Playing";
      }
      return "?";
    }

    const char* name(QueueChangeReason reason) {
      switch (reason) {
      case QueueChangeReason::NewPlaylist: return "NewPlaylist";
      case QueueChangeReason::SetCurrentItem: return "SetCurrentItem";
      case QueueChangeReason::NextItem: return "NextItem";
      case QueueChangeReason::PreviousItem: return "PreviousItem";
      case QueueChangeReason::RemoveItems: return "RemoveItems";
      case QueueChangeReason::MoveItem: return "MoveItem";
      case QueueChangeReason::Queue: return "Queue";
      case QueueChangeReason::QueueNext: return "QueueNext";
      case QueueChangeReason::RepeatMode: return "RepeatMode";
      case QueueChangeReason::ShuffleMode: return "ShuffleMode";
      case QueueChangeReason::Unknown: return "Unknown";
      }
      return "?";
    }

    const char* name(TimerPurpose purpose) {
      switch (purpose) {
      case TimerPurpose::GroupCommand: return "GroupCommand";
      case TimerPurpose::CorrectionCooldown: return "CorrectionCooldown";
      case TimerPurpose::CorrectionEnd: return "CorrectionEnd";
      case TimerPurpose::ReadyDeadline: return "ReadyDeadline";
      }
      return "?";
    }

    const char* name(TrackKind kind) { return kind == TrackKind::Audio ? "audio" : "subtitle"; }

    const char* name(backend::EndReason reason) {
      switch (reason) {
      case backend::EndReason::EndOfFile: return "end of file";
      case backend::EndReason::Stopped: return "stopped";
      case backend::EndReason::Error: return "error";
      }
      return "?";
    }

    std::string flags(const backend::Sample& sample) {
      std::string text = seconds(sample.position);
      if (sample.paused) text += " paused";
      if (sample.seeking) text += " seeking";
      if (sample.buffering) text += " buffering";
      return text;
    }

    std::string item(const PlaylistItemId& playlistItem) {
      return playlistItem.isEmpty() ? std::string("-") : playlistItem.value();
    }
  } // namespace

  std::string describe(const SessionInput& input) {
    return std::visit(
        Overloaded{
            // The request's URL carries the access token: never recorded.
            [](const intent::Open& open) {
              return "Open item " + open.media.item.value() + " playlist " +
                     item(open.media.playlistItem) + " at " + seconds(open.media.start) +
                     (open.media.startPaused ? " paused" : "");
            },
            [](const intent::Play&) { return std::string("Play"); },
            [](const intent::Pause&) { return std::string("Pause"); },
            [](const intent::Seek& seek) { return "Seek " + seconds(seek.position); },
            [](const intent::SetRate& set) { return "SetRate " + rate(set.rate); },
            [](const intent::Stop&) { return std::string("Stop"); },
            [](const intent::Next&) { return std::string("Next"); },
            [](const intent::Previous&) { return std::string("Previous"); },
            [](const intent::SelectTrack& select) {
              return "SelectTrack " + epoch("media", select.epoch) + " " + name(select.kind) +
                     " " + (select.trackId ? std::to_string(*select.trackId) : std::string("off"));
            },
            [](const intent::AddExternalTrack& add) {
              return "AddExternalTrack " + epoch("media", add.epoch) + " " + name(add.track.kind) +
                     (add.track.language.empty() ? std::string() : " " + add.track.language);
            },
            [](const intent::PreloadNext& next) {
              return "PreloadNext item " + next.media.item.value();
            },
            [](const intent::SetSyncCorrection& set) {
              return std::string("SetSyncCorrection speed ") +
                     (set.reconciler.useSpeed ? "on" : "off") + " skip " +
                     std::to_string(set.reconciler.skipMinimum.milliseconds()) + "ms";
            },
            [](const backend::SourceOpened& opened) {
              return "SourceOpened " + epoch("media", opened.epoch);
            },
            [](const backend::SeekStarted& started) {
              return "SeekStarted " + epoch("media", started.epoch);
            },
            [](const backend::PlaybackRestarted& restarted) {
              return "PlaybackRestarted " + epoch("media", restarted.epoch) + " " +
                     flags(restarted.sample);
            },
            [](const backend::Progress& progress) {
              return "Progress " + epoch("media", progress.epoch) + " " + flags(progress.sample);
            },
            [](const backend::Ended& ended) {
              // Backend errors can quote the stream URL: only the reason is kept.
              return "Ended " + epoch("media", ended.epoch) + " " + name(ended.reason);
            },
            [](const backend::RequestFailed& failed) {
              return "RequestFailed " + epoch("media", failed.epoch) + " " +
                     epoch("request", failed.request);
            },
            [](const backend::Advanced& advanced) {
              return "Advanced " + epoch("media", advanced.from) + " -> " +
                     epoch("media", advanced.to);
            },
            [](const backend::TrackAdded& added) {
              return "TrackAdded " + epoch("media", added.epoch) + " " +
                     epoch("request", added.request) + " track " + std::to_string(added.trackId);
            },
            [](const group::Joined& joined) {
              return "Joined " + epoch("group", joined.epoch) + " " + joined.group.value() + " (" +
                     std::to_string(joined.participants.size()) + " members)";
            },
            [](const group::Left& left) { return "Left " + epoch("group", left.epoch); },
            [](const group::ParticipantsChanged& changed) {
              return "ParticipantsChanged " + epoch("group", changed.epoch) + " " +
                     std::to_string(changed.participants.size()) + " members";
            },
            [](const group::ConnectionLost& lost) {
              return "ConnectionLost " + epoch("group", lost.epoch);
            },
            [](const group::StateChanged& changed) {
              return "StateChanged " + epoch("group", changed.epoch) + " " + name(changed.state);
            },
            [](const group::Command& command) {
              return std::string("Command ") + name(command.type) + " " +
                     epoch("group", command.epoch) + " playlist " + item(command.playlistItem) +
                     " at " + seconds(command.position) + " when " + clock(command.when) +
                     " emitted " + clock(command.emittedAt) + " " + rate(command.baseRate);
            },
            [](const group::QueueChanged& queue) {
              return std::string("QueueChanged ") + name(queue.reason) + " " +
                     epoch("group", queue.epoch) + " playlist " +
                     item(queue.currentPlaylistItem) + " at " + seconds(queue.startPosition) +
                     (queue.isPlaying ? " playing " : " ") + rate(queue.baseRate) + " updated " +
                     clock(queue.lastUpdate);
            },
            [](const group::RateChangeRejected& rejected) {
              return "RateChangeRejected " + epoch("group", rejected.epoch) + " HTTP " +
                     std::to_string(rejected.httpStatus);
            },
            [](const group::ClockSynced& synced) {
              const auto offset =
                  std::chrono::duration_cast<std::chrono::milliseconds>(synced.offset.serverMinusLocal);
              const auto roundTrip =
                  std::chrono::duration_cast<std::chrono::milliseconds>(synced.roundTrip);
              return "ClockSynced round trip " + std::to_string(roundTrip.count()) + "ms offset " +
                     std::to_string(offset.count()) + "ms";
            },
            [](const TimerFired& fired) {
              return std::string("TimerFired ") + name(fired.id.purpose) + " #" +
                     std::to_string(fired.id.serial);
            },
        },
        input);
  }

  std::string describe(const SyncPlayMessage& message) {
    return std::visit(
        Overloaded{
            [](const outgoing::Ready& ready) {
              return "Ready " + epoch("group", ready.epoch) + " playlist " +
                     item(ready.playlistItem) + " at " + seconds(ready.position) + " when " +
                     clock(ready.when) + (ready.isPlaying ? " playing" : " paused");
            },
            [](const outgoing::Buffering& buffering) {
              return "Buffering " + epoch("group", buffering.epoch) + " playlist " +
                     item(buffering.playlistItem) + " at " + seconds(buffering.position) +
                     " when " + clock(buffering.when) +
                     (buffering.isPlaying ? " playing" : " paused");
            },
            [](const outgoing::RequestPause& request) {
              return "RequestPause " + epoch("group", request.epoch);
            },
            [](const outgoing::RequestUnpause& request) {
              return "RequestUnpause " + epoch("group", request.epoch);
            },
            [](const outgoing::RequestSeek& request) {
              return "RequestSeek " + epoch("group", request.epoch) + " " +
                     seconds(request.position);
            },
            [](const outgoing::RequestPlaybackRate& request) {
              return "RequestPlaybackRate " + epoch("group", request.epoch) + " " +
                     rate(request.rate);
            },
            [](const outgoing::RequestStop& request) {
              return "RequestStop " + epoch("group", request.epoch);
            },
            [](const outgoing::RequestNextItem& request) {
              return "RequestNextItem " + epoch("group", request.epoch) + " playlist " +
                     item(request.playlistItem);
            },
            [](const outgoing::RequestPreviousItem& request) {
              return "RequestPreviousItem " + epoch("group", request.epoch) + " playlist " +
                     item(request.playlistItem);
            },
        },
        message);
  }

  SessionJournal::SessionJournal(std::size_t capacity) : m_capacity(capacity > 0 ? capacity : 1) {}

  void SessionJournal::input(LocalInstant at, const SessionInput& input, bool accepted) {
    if (const auto* progress = std::get_if<backend::Progress>(&input); progress && accepted) {
      const backend::Sample& sample = progress->sample;
      const bool changed = !m_haveFlags || sample.paused != m_paused ||
                           sample.seeking != m_seeking || sample.buffering != m_buffering;
      m_haveFlags = true;
      m_paused = sample.paused;
      m_seeking = sample.seeking;
      m_buffering = sample.buffering;
      if (!changed) return;
    }
    add(at, accepted ? "in" : "dropped", describe(input));
  }

  void SessionJournal::output(LocalInstant at, const SyncPlayMessage& message) {
    add(at, "out", describe(message));
  }

  void SessionJournal::add(LocalInstant at, const char* direction, std::string text) {
    if (m_entries.size() == m_capacity) m_entries.pop_front();
    m_entries.push_back(JournalEntry{at, direction, std::move(text)});
    ++m_recorded;
  }

  std::vector<JournalEntry> SessionJournal::entries() const {
    return {m_entries.begin(), m_entries.end()};
  }

  std::vector<std::string> SessionJournal::lines() const {
    std::vector<std::string> result;
    result.reserve(m_entries.size());
    for (const JournalEntry& entry : m_entries) {
      const std::int64_t ms = entry.at.sinceOrigin().count() / 10'000;
      char stamp[32];
      std::snprintf(stamp, sizeof stamp, "%" PRId64 ".%03" PRId64, ms / 1000, ms % 1000);
      result.push_back(std::string(stamp) + " " + entry.direction + " " + entry.text);
    }
    return result;
  }

} // namespace jfd::core
