#pragma once

#include <jfd/core/MediaBackend.h>
#include <jfd/core/SessionInput.h>
#include <jfd/core/Timers.h>

#include <mpv/client.h>

#include <QByteArray>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace jfd::media {

class MpvPlayer;

// core::MediaBackend on an MpvPlayer. Runs on the player's (GUI) thread, like
// the playback session, and hands every backend event to `post` as a
// SessionInput tagged with the epoch of the file that produced it.
//
// - load() sets pause and rebase-start-time, then loadfile; the file is held
//   in mpv's on_preloaded hook and reported as SourceOpened with
//   demuxer-start-time. start() sets the file's start position and releases
//   the hook, so the first frame mpv decodes is the requested one.
// - Files are matched to epochs in loadfile order through MPV_EVENT_START_FILE
//   and the playlist entry id that later events carry.
// - preload() appends the next file to mpv's playlist (prefetched ahead of
//   time) and tells the session about the switch with Advanced when the
//   current file ends by itself; the file is not held at the start.
// - PlaybackRestarted and Progress carry a sample read after the event queue
//   is drained. Progress is posted on any pause, seeking or buffering change
//   and at most every 250 ms for position alone.
class MpvMediaBackend final : public core::MediaBackend
{
public:
  using Post = std::function<void(core::SessionInput)>;

  MpvMediaBackend(MpvPlayer& player, const core::Clock& clock, Post post);
  ~MpvMediaBackend() override;

  MpvMediaBackend(const MpvMediaBackend&) = delete;
  MpvMediaBackend& operator=(const MpvMediaBackend&) = delete;

  void load(core::MediaEpoch epoch, const core::BackendLoad& request) override;
  void preload(core::MediaEpoch epoch, const core::BackendLoad& request) override;
  void start(core::MediaEpoch epoch, std::optional<core::BackendSeconds> position) override;
  void seek(core::MediaEpoch epoch, core::RequestId request, core::BackendSeconds target) override;
  void setPaused(core::MediaEpoch epoch, bool paused) override;
  void setRate(core::MediaEpoch epoch, core::PlaybackRate rate) override;
  void selectTrack(core::MediaEpoch epoch, core::TrackKind kind, std::optional<int> trackId) override;
  void addExternalTrack(core::MediaEpoch epoch, core::RequestId request,
                        const core::ExternalTrack& track) override;
  void stop(core::MediaEpoch epoch) override;

private:
  enum class Reply : std::uint8_t { Load, Seek, AddTrack };
  struct Pending
  {
    Reply kind;
    core::MediaEpoch epoch;
    core::RequestId request;
    core::TrackKind trackKind = core::TrackKind::Audio;
    std::string url;
  };

  void handle(const mpv_event& event);
  void handleReply(std::uint64_t token, const mpv_event& event);
  void flush();
  core::backend::Sample sample() const;
  bool isCurrent(core::MediaEpoch epoch) const;
  void releaseHook();
  // Forgets the queued file: it leaves mpv's playlist and the pending starts.
  void dropPreload();
  std::uint64_t command(const Pending& pending, std::initializer_list<const char*> arguments);
  std::optional<int> findExternalTrack(core::TrackKind kind, const std::string& url) const;

  MpvPlayer& m_player;
  mpv_handle* m_mpv;
  const core::Clock& m_clock;
  Post m_post;

  // Loads sent to mpv whose START_FILE has not arrived yet, in order.
  std::deque<core::MediaEpoch> m_starting;
  std::map<std::int64_t, core::MediaEpoch> m_entries;
  // The file mpv is playing or holding, if any.
  std::optional<core::MediaEpoch> m_current;
  // Track choices (aid, sid; empty: mpv's own) for files not started yet.
  std::map<core::MediaEpoch, std::array<QByteArray, 2>> m_startTracks;
  std::optional<std::uint64_t> m_hook;
  // The file queued behind the current one (preload()), and the file mpv has
  // just finished while it waits for the queued one to start.
  std::optional<core::MediaEpoch> m_preload;
  // How the file mpv left ended, reported once the queued file has started.
  std::optional<core::backend::Ended> m_leaving;
  // The queued file that has just become current: not held at its start.
  std::optional<core::MediaEpoch> m_gapless;
  // The file mpv went on to by itself: its first restart can report a
  // position just before zero (timestamps carry on from the file before).
  bool m_followedOn = false;
  std::map<std::uint64_t, Pending> m_pending;
  std::uint64_t m_nextToken = 1;

  bool m_restartPending = false;
  bool m_stateChanged = false;
  bool m_positionChanged = false;
  std::optional<core::LocalInstant> m_lastProgress;
};

}
