#pragma once

#include <jfd/api/Playback.h>
#include <jfd/core/SessionInput.h>

#include <optional>

// Turns a resolved stream into what the playback session opens, and asks for
// the stream again when it has to be restarted (a seek before a progressive
// transcode's start, or a track the transcode does not carry).
namespace jfd::playback {

  // How backend positions map to the item's timeline:
  // - direct play and stream, and HLS: backend time is media time;
  // - a progressive (non-HLS) transcode starts its clock at the requested
  //   start, so media = backend + start (TranscodingOffsetTicks);
  // - a transcode with CopyTimestamps=true keeps the source's timestamps:
  //   PreservedVideo, or PreservedMusic for audio, whose clock mpv rebases.
  core::StreamTimeline timelineFor(const api::PlaybackStream& stream, bool audioOnly,
                                   core::MediaTicks start);

  // The session's request for one resolved stream.
  core::MediaRequest mediaRequest(const api::PlaybackStream& stream, bool audioOnly,
                                  core::MediaTicks start, std::optional<core::MediaTicks> runTime,
                                  bool startPaused);

  // PlaybackInfo again for the same item and source, from `position`, with
  // the given tracks (-1: subtitles off). Everything else as in `previous`.
  api::PlaybackInfoRequest reopenRequest(const api::PlaybackInfoRequest& previous,
                                         const api::PlaybackStream& stream,
                                         core::MediaTicks position,
                                         std::optional<int> audioStreamIndex,
                                         int subtitleStreamIndex);

} // namespace jfd::playback
