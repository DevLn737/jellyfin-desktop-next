#include "MpvMediaBackend.h"
#include "MpvPlayer.h"

#include <QByteArray>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

namespace jfd::media {

namespace {

// reply_userdata of the adapter's hook and observed properties. Command
// replies use their own counter; the event types keep them apart.
constexpr std::uint64_t PreloadedHook = 0x6a66640001;
enum Observed : std::uint64_t {
  TimePos = 0x6a66640100,
  Pause,
  Seeking,
  PausedForCache,
  EofReached,
};

constexpr auto ProgressInterval = std::chrono::milliseconds(250);

QByteArray seconds(double value)
{
  return QByteArray::number(value, 'f', 6);
}

std::optional<double> doubleProperty(mpv_handle* mpv, const char* name)
{
  double value = 0;
  if (mpv_get_property(mpv, name, MPV_FORMAT_DOUBLE, &value) < 0 || !std::isfinite(value))
    return std::nullopt;
  return value;
}

bool flagProperty(mpv_handle* mpv, const char* name)
{
  int value = 0;
  return mpv_get_property(mpv, name, MPV_FORMAT_FLAG, &value) >= 0 && value != 0;
}

// File-local, so a choice never carries over to the next file, where the
// same number can be a different track or none.
const char* trackProperty(core::TrackKind kind)
{
  return kind == core::TrackKind::Audio ? "file-local-options/aid" : "file-local-options/sid";
}

}

MpvMediaBackend::MpvMediaBackend(MpvPlayer& player, const core::Clock& clock, Post post)
  : m_player(player), m_mpv(player.handle().get()), m_clock(clock), m_post(std::move(post))
{
  if (!m_mpv)
    return;
  mpv_hook_add(m_mpv, PreloadedHook, "on_preloaded", 0);
  mpv_observe_property(m_mpv, TimePos, "time-pos", MPV_FORMAT_NONE);
  mpv_observe_property(m_mpv, Pause, "pause", MPV_FORMAT_NONE);
  mpv_observe_property(m_mpv, Seeking, "seeking", MPV_FORMAT_NONE);
  mpv_observe_property(m_mpv, PausedForCache, "paused-for-cache", MPV_FORMAT_NONE);
  mpv_observe_property(m_mpv, EofReached, "eof-reached", MPV_FORMAT_FLAG);
  m_player.setEventListener([this](const mpv_event& event) { handle(event); });
}

MpvMediaBackend::~MpvMediaBackend()
{
  if (!m_mpv)
    return;
  m_player.setEventListener({});
  for (const std::uint64_t id : {TimePos, Pause, Seeking, PausedForCache, EofReached})
    mpv_unobserve_property(m_mpv, id);
  releaseHook();
}

bool MpvMediaBackend::isCurrent(core::MediaEpoch epoch) const
{
  return m_current && *m_current == epoch;
}

void MpvMediaBackend::releaseHook()
{
  if (m_hook)
    mpv_hook_continue(m_mpv, *m_hook);
  m_hook.reset();
}

std::uint64_t MpvMediaBackend::command(const Pending& pending, std::initializer_list<const char*> arguments)
{
  std::vector<const char*> list(arguments);
  list.push_back(nullptr);
  const std::uint64_t token = m_nextToken++;
  m_pending.emplace(token, pending);
  if (const int error = mpv_command_async(m_mpv, token, list.data()); error < 0)
  {
    // Rejected before mpv queued it; report it like a failed reply.
    mpv_event event {};
    event.event_id = MPV_EVENT_COMMAND_REPLY;
    event.error = error;
    handleReply(token, event);
  }
  return token;
}

void MpvMediaBackend::load(core::MediaEpoch epoch, const core::BackendLoad& request)
{
  if (!m_mpv)
  {
    m_post(core::backend::Ended {epoch, core::backend::EndReason::Error, "mpv is not available"});
    return;
  }
  // A held file must be released, or mpv waits in the hook for ever.
  releaseHook();
  // loadfile replace clears the playlist, the queued file with it.
  dropPreload();
  mpv_set_property_string(m_mpv, "rebase-start-time", request.rebaseStartTime ? "yes" : "no");
  int pause = request.startPaused ? 1 : 0;
  mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pause);
  m_starting.push_back(epoch);
  command({Reply::Load, epoch, {}, {}, {}}, {"loadfile", request.url.c_str(), "replace"});
}

void MpvMediaBackend::preload(core::MediaEpoch epoch, const core::BackendLoad& request)
{
  if (!m_mpv || !m_current)
  {
    m_post(core::backend::Ended {epoch, core::backend::EndReason::Error, "no file is playing to follow"});
    return;
  }
  dropPreload();
  m_preload = epoch;
  m_starting.push_back(epoch);
  // Per-file options: the property is global, and the current file keeps its own.
  const char* options = request.rebaseStartTime ? "rebase-start-time=yes" : "rebase-start-time=no";
  // mpv 0.38 (client API 2.3) added the index between the flags and the
  // options; older ones take the options right after the flags.
  if (mpv_client_api_version() >= MPV_MAKE_VERSION(2, 3))
    command({Reply::Load, epoch, {}, {}, {}}, {"loadfile", request.url.c_str(), "append", "-1", options});
  else
    command({Reply::Load, epoch, {}, {}, {}}, {"loadfile", request.url.c_str(), "append", options});
}

void MpvMediaBackend::dropPreload()
{
  m_leaving.reset();
  m_gapless.reset();
  if (!m_preload)
    return;
  std::erase(m_starting, *m_preload);
  m_preload.reset();
  // Everything but the current file.
  mpv_command_string(m_mpv, "playlist-clear");
}

void MpvMediaBackend::start(core::MediaEpoch epoch, std::optional<core::BackendSeconds> position)
{
  if (!isCurrent(epoch) || !m_hook)
    return;
  // Read by mpv after on_preloaded, for the first seek of this file only.
  mpv_set_property_string(m_mpv, "file-local-options/start",
                          position ? seconds(position->value).constData() : "none");
  releaseHook();
}

void MpvMediaBackend::seek(core::MediaEpoch epoch, core::RequestId request, core::BackendSeconds target)
{
  if (!isCurrent(epoch))
  {
    m_post(core::backend::RequestFailed {epoch, request, "no such file"});
    return;
  }
  const QByteArray value = seconds(target.value);
  command({Reply::Seek, epoch, request, {}, {}}, {"seek", value.constData(), "absolute+exact"});
}

void MpvMediaBackend::setPaused(core::MediaEpoch epoch, bool paused)
{
  if (!isCurrent(epoch))
    return;
  int flag = paused ? 1 : 0;
  mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
}

void MpvMediaBackend::setRate(core::MediaEpoch epoch, core::PlaybackRate rate)
{
  if (!isCurrent(epoch))
    return;
  double speed = rate.value();
  mpv_set_property_async(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE, &speed);
}

void MpvMediaBackend::selectTrack(core::MediaEpoch epoch, core::TrackKind kind, std::optional<int> trackId)
{
  const QByteArray value = trackId ? QByteArray::number(*trackId) : QByteArray("no");
  if (isCurrent(epoch))
  {
    mpv_set_property_string(m_mpv, trackProperty(kind), value.constData());
    return;
  }
  // The first choice usually comes while mpv is still opening the file:
  // applied once it starts, before its tracks are selected.
  if (std::find(m_starting.begin(), m_starting.end(), epoch) != m_starting.end())
    m_startTracks[epoch][kind == core::TrackKind::Audio ? 0 : 1] = value;
}

void MpvMediaBackend::addExternalTrack(core::MediaEpoch epoch, core::RequestId request,
                                       const core::ExternalTrack& track)
{
  if (!isCurrent(epoch))
  {
    m_post(core::backend::RequestFailed {epoch, request, "no such file"});
    return;
  }
  const char* name = track.kind == core::TrackKind::Audio ? "audio-add" : "sub-add";
  command({Reply::AddTrack, epoch, request, track.kind, track.url},
          {name, track.url.c_str(), "select", track.title.c_str(), track.language.c_str()});
}

void MpvMediaBackend::stop(core::MediaEpoch epoch)
{
  if (!m_mpv || (!isCurrent(epoch) && m_starting.empty()))
    return;
  releaseHook();
  dropPreload();
  mpv_command_string(m_mpv, "stop");
}

std::optional<int> MpvMediaBackend::findExternalTrack(core::TrackKind kind, const std::string& url) const
{
  mpv_node list;
  if (mpv_get_property(m_mpv, "track-list", MPV_FORMAT_NODE, &list) < 0)
    return std::nullopt;
  const char* type = kind == core::TrackKind::Audio ? "audio" : "sub";
  std::optional<int> found;
  if (list.format == MPV_FORMAT_NODE_ARRAY)
  {
    for (int i = 0; i < list.u.list->num; ++i)
    {
      const mpv_node& entry = list.u.list->values[i];
      if (entry.format != MPV_FORMAT_NODE_MAP)
        continue;
      std::optional<std::int64_t> id;
      bool typeMatches = false;
      bool urlMatches = false;
      for (int k = 0; k < entry.u.list->num; ++k)
      {
        const std::string key = entry.u.list->keys[k];
        const mpv_node& value = entry.u.list->values[k];
        if (key == "id" && value.format == MPV_FORMAT_INT64)
          id = value.u.int64;
        else if (key == "type" && value.format == MPV_FORMAT_STRING)
          typeMatches = std::string(value.u.string) == type;
        else if (key == "external-filename" && value.format == MPV_FORMAT_STRING)
          urlMatches = url == value.u.string;
      }
      // The newest track added from this URL.
      if (id && typeMatches && urlMatches && (!found || *id > *found))
        found = static_cast<int>(*id);
    }
  }
  mpv_free_node_contents(&list);
  return found;
}

void MpvMediaBackend::handleReply(std::uint64_t token, const mpv_event& event)
{
  const auto it = m_pending.find(token);
  if (it == m_pending.end())
    return;
  const Pending pending = it->second;
  m_pending.erase(it);
  const bool failed = event.error < 0;
  const std::string error = failed ? mpv_error_string(event.error) : std::string();
  switch (pending.kind)
  {
  case Reply::Load:
    // A loadfile that failed never starts, so it gets no START_FILE.
    if (failed)
    {
      std::erase(m_starting, pending.epoch);
      m_post(core::backend::Ended {pending.epoch, core::backend::EndReason::Error, error});
    }
    break;
  case Reply::Seek:
    if (failed)
      m_post(core::backend::RequestFailed {pending.epoch, pending.request, error});
    break;
  case Reply::AddTrack:
    if (failed)
    {
      m_post(core::backend::RequestFailed {pending.epoch, pending.request, error});
    }
    else if (const auto id = findExternalTrack(pending.trackKind, pending.url))
    {
      m_post(core::backend::TrackAdded {pending.epoch, pending.request, *id});
    }
    else
    {
      m_post(core::backend::RequestFailed {pending.epoch, pending.request, "the added track is missing"});
    }
    break;
  }
}

void MpvMediaBackend::handle(const mpv_event& event)
{
  switch (event.event_id)
  {
  case MPV_EVENT_NONE:
    flush();
    break;
  case MPV_EVENT_COMMAND_REPLY:
    handleReply(event.reply_userdata, event);
    break;
  case MPV_EVENT_START_FILE:
  {
    const auto* start = static_cast<const mpv_event_start_file*>(event.data);
    if (m_starting.empty())
    {
      // A file the adapter did not load (the QML player); not the session's.
      m_current.reset();
      break;
    }
    m_current = m_starting.front();
    m_starting.pop_front();
    m_entries[start->playlist_entry_id] = *m_current;
    if (m_preload && *m_preload == *m_current)
    {
      // The queued file starts: the one before it has ended.
      m_preload.reset();
      if (m_leaving)
      {
        const core::backend::Ended left = *m_leaving;
        m_leaving.reset();
        if (left.reason == core::backend::EndReason::EndOfFile)
        {
          m_gapless = *m_current;
          m_post(core::backend::Advanced {left.epoch, *m_current});
        }
        else
        {
          // The session was told the file failed or stopped; it does not
          // follow on into the queued one.
          m_post(left);
          mpv_command_string(m_mpv, "stop");
        }
      }
    }
    if (const auto tracks = m_startTracks.extract(*m_current))
    {
      for (const core::TrackKind kind : {core::TrackKind::Audio, core::TrackKind::Subtitle})
      {
        const QByteArray& value = tracks.mapped()[kind == core::TrackKind::Audio ? 0 : 1];
        if (!value.isEmpty())
          mpv_set_property_string(m_mpv, trackProperty(kind), value.constData());
      }
    }
    // Choices for loads that were replaced before they started.
    std::erase_if(m_startTracks, [this](const auto& entry) {
      return std::find(m_starting.begin(), m_starting.end(), entry.first) == m_starting.end();
    });
    m_restartPending = false;
    m_lastProgress.reset();
    break;
  }
  case MPV_EVENT_HOOK:
  {
    const auto* hook = static_cast<const mpv_event_hook*>(event.data);
    if (event.reply_userdata != PreloadedHook)
      break;
    if (!m_current)
    {
      mpv_hook_continue(m_mpv, hook->id);
      break;
    }
    m_post(core::backend::SourceOpened {*m_current, doubleProperty(m_mpv, "demuxer-start-time"),
                                        doubleProperty(m_mpv, "duration")});
    if (m_gapless == m_current)
    {
      // A file that followed on without a gap is not held for start().
      m_gapless.reset();
      mpv_hook_continue(m_mpv, hook->id);
      break;
    }
    m_hook = hook->id;
    break;
  }
  case MPV_EVENT_SEEK:
    if (m_current)
      m_post(core::backend::SeekStarted {*m_current});
    break;
  case MPV_EVENT_PLAYBACK_RESTART:
    if (m_current)
      m_restartPending = true;
    break;
  case MPV_EVENT_PROPERTY_CHANGE:
    switch (event.reply_userdata)
    {
    case TimePos:
      m_positionChanged = true;
      break;
    case Pause:
    case Seeking:
    case PausedForCache:
      m_stateChanged = true;
      break;
    case EofReached:
    {
      const auto* property = static_cast<const mpv_event_property*>(event.data);
      // keep-open=yes holds the last frame at the end instead of ending the file.
      // With a file queued mpv goes on to it instead of holding the last frame.
      if (m_current && !m_preload && property->format == MPV_FORMAT_FLAG
          && *static_cast<int*>(property->data))
        m_post(core::backend::Ended {*m_current, core::backend::EndReason::EndOfFile, {}});
      break;
    }
    default:
      break;
    }
    break;
  case MPV_EVENT_END_FILE:
  {
    const auto* end = static_cast<const mpv_event_end_file*>(event.data);
    const auto entry = m_entries.find(end->playlist_entry_id);
    if (entry == m_entries.end())
      break;
    const core::MediaEpoch epoch = entry->second;
    m_entries.erase(entry);
    const bool wasCurrent = isCurrent(epoch);
    if (wasCurrent)
    {
      m_current.reset();
      m_hook.reset();
      m_gapless.reset();
      m_restartPending = false;
    }
    if (wasCurrent && m_preload)
    {
      // mpv goes on to the queued file; the session hears how this one ended
      // when that file has started (Advanced after a natural end).
      core::backend::EndReason reason = core::backend::EndReason::Stopped;
      std::string error;
      if (end->reason == MPV_END_FILE_REASON_EOF)
        reason = core::backend::EndReason::EndOfFile;
      else if (end->reason == MPV_END_FILE_REASON_ERROR)
      {
        reason = core::backend::EndReason::Error;
        error = mpv_error_string(end->error);
      }
      m_leaving = core::backend::Ended {epoch, reason, error};
      break;
    }
    switch (end->reason)
    {
    case MPV_END_FILE_REASON_EOF:
      m_post(core::backend::Ended {epoch, core::backend::EndReason::EndOfFile, {}});
      break;
    case MPV_END_FILE_REASON_ERROR:
      m_post(core::backend::Ended {epoch, core::backend::EndReason::Error, mpv_error_string(end->error)});
      break;
    default:
      m_post(core::backend::Ended {epoch, core::backend::EndReason::Stopped, {}});
      break;
    }
    break;
  }
  default:
    break;
  }
}

void MpvMediaBackend::flush()
{
  if (!m_current)
  {
    m_stateChanged = m_positionChanged = false;
    return;
  }
  const core::MediaEpoch epoch = *m_current;
  if (m_restartPending)
  {
    m_restartPending = false;
    m_stateChanged = m_positionChanged = false;
    const core::backend::Sample now = sample();
    m_lastProgress = now.sampledAt;
    m_post(core::backend::PlaybackRestarted {epoch, now});
    return;
  }
  if (!m_stateChanged && !m_positionChanged)
    return;
  const core::LocalInstant now = m_clock.now();
  if (!m_stateChanged && m_lastProgress
      && now - *m_lastProgress < std::chrono::duration_cast<core::WallTicks>(ProgressInterval))
    return;
  m_stateChanged = m_positionChanged = false;
  m_lastProgress = now;
  m_post(core::backend::Progress {epoch, sample()});
}

core::backend::Sample MpvMediaBackend::sample() const
{
  core::backend::Sample result;
  if (const auto position = doubleProperty(m_mpv, "time-pos"))
    result.position = core::BackendSeconds {*position};
  result.paused = flagProperty(m_mpv, "pause");
  result.seeking = flagProperty(m_mpv, "seeking");
  result.buffering = flagProperty(m_mpv, "paused-for-cache");
  result.sampledAt = m_clock.now();
  return result;
}

}
