#pragma once

#include "Fakes.h"

#include <jfd/core/PlaybackSession.h>

#include <memory>

namespace jfd::testing {

  // A session wired to recording fakes, with helpers that play the backend's
  // and the server's part in scripted tests.
  struct SessionHarness {
    FakeClock clock;
    FakeTimers timers;
    RecordingBackend backend;
    RecordingOutbox outbox;
    RecordingObserver observer;
    std::unique_ptr<PlaybackSession> session;
    EpochCounter<GroupEpochTag> groupEpochs;
    GroupEpoch group;

    explicit SessionHarness(SessionSettings settings = {}) {
      session = std::make_unique<PlaybackSession>(
          SessionPorts{backend, clock, timers, outbox, observer}, settings);
    }

    static MediaTicks ms(std::int64_t value) { return *MediaTicks::fromMilliseconds(value); }

    void post(SessionInput input) { session->post(std::move(input)); }

    // Advances fake time, firing due timers in order.
    void advance(WallTicks by) {
      const LocalInstant end = clock.now() + by;
      while (true) {
        const auto next = timers.next();
        if (!next || next->second > end) break;
        clock.set(next->second);
        for (const auto& id : timers.takeDue(clock.now())) post(TimerFired{id});
      }
      clock.set(end);
    }

    backend::Sample sample(double seconds, bool paused, bool buffering = false) const {
      return backend::Sample{BackendSeconds{seconds}, paused, false, buffering, clock.now()};
    }

    MediaEpoch currentEpoch() const { return backend.last<command::Load>()->epoch; }

    // Opens a file and completes the load at `seconds`.
    MediaEpoch open(MediaRequest request, bool paused = false) {
      post(intent::Open{request});
      const MediaEpoch epoch = currentEpoch();
      post(backend::SourceOpened{epoch, 0.0, 600.0});
      const double start = request.start.seconds();
      post(backend::PlaybackRestarted{epoch, sample(start, paused)});
      return epoch;
    }

    MediaRequest media(std::string playlistItem = {}, std::int64_t startMs = 0) const {
      MediaRequest request;
      request.item = ItemId("item");
      request.mediaSource = MediaSourceId("source");
      request.playlistItem = PlaylistItemId(std::move(playlistItem));
      request.url = "http://server/video";
      request.start = ms(startMs);
      return request;
    }

    void join(std::string playlistItem = "p1", PlaybackRate base = PlaybackRate::normal()) {
      group = groupEpochs.advance();
      post(group::Joined{group, GroupId("g")});
      group::QueueChanged queue;
      queue.epoch = group;
      queue.reason = QueueChangeReason::NewPlaylist;
      queue.lastUpdate = serverNow();
      queue.currentPlaylistItem = PlaylistItemId(std::move(playlistItem));
      queue.currentItem = ItemId("item");
      queue.baseRate = base;
      post(queue);
    }

    ServerInstant serverNow() const { return ClockOffset{}.toServer(clock.now()); }

    group::Command command(GroupCommandType type, std::int64_t positionMs,
                           WallTicks whenFromNow = WallTicks(0),
                           PlaybackRate base = PlaybackRate::normal(),
                           std::string playlistItem = "p1") const {
      group::Command c;
      c.epoch = group;
      c.type = type;
      c.playlistItem = PlaylistItemId(std::move(playlistItem));
      c.when = serverNow() + whenFromNow;
      c.emittedAt = serverNow();
      c.position = ms(positionMs);
      c.baseRate = base;
      return c;
    }

    const SessionSnapshot& snapshot() const { return session->snapshot(); }
  };

} // namespace jfd::testing
