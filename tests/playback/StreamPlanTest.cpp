#include "../support/DoctestQt.h"

#include <jfd/playback/StreamPlan.h>

using namespace jfd;
using namespace jfd::playback;
using Kind = core::StreamTimeline::Kind;

namespace {

  api::PlaybackStream played(api::PlayMethod method, const char* url, bool hls = false) {
    api::PlaybackStream stream;
    stream.method = method;
    stream.url = QUrl(QString::fromLatin1(url));
    stream.isHls = hls;
    stream.itemId = QStringLiteral("item");
    stream.mediaSourceId = QStringLiteral("source");
    return stream;
  }

  const core::MediaTicks start = *core::MediaTicks::fromMilliseconds(754'000);

} // namespace

TEST_CASE("Direct streams and HLS keep the item's timeline") {
  for (const auto& stream :
       {played(api::PlayMethod::DirectPlay, "https://srv/Videos/item/stream.mkv?Static=true"),
        played(api::PlayMethod::DirectStream, "https://srv/Videos/item/stream.mkv?Static=true"),
        played(api::PlayMethod::Transcode, "https://srv/videos/item/master.m3u8?StartTimeTicks=1",
               true)}) {
    const core::StreamTimeline timeline = timelineFor(stream, false, start);
    CHECK(timeline.kind == Kind::Relative);
    CHECK(timeline.offset.ticks() == 0);
  }
}

TEST_CASE("A progressive transcode starts its clock at the requested start") {
  const auto stream = played(api::PlayMethod::Transcode, "https://srv/videos/item/stream.mp4");
  const core::StreamTimeline timeline = timelineFor(stream, false, start);
  CHECK(timeline.kind == Kind::Relative);
  CHECK(timeline.offset == start);
}

TEST_CASE("CopyTimestamps keeps the source's timestamps") {
  const auto stream =
      played(api::PlayMethod::Transcode, "https://srv/videos/item/master.m3u8?copyTimestamps=TRUE", true);
  CHECK(timelineFor(stream, false, start).kind == Kind::PreservedVideo);
  CHECK(timelineFor(stream, true, start).kind == Kind::PreservedMusic);
  const auto off =
      played(api::PlayMethod::Transcode, "https://srv/videos/item/stream.mp4?CopyTimestamps=false");
  CHECK(timelineFor(off, false, start).offset == start);
}

TEST_CASE("The session's request carries the stream, start and timeline") {
  const auto stream = played(api::PlayMethod::Transcode, "https://srv/videos/item/stream.mp4?ApiKey=t");
  const core::MediaRequest request =
      mediaRequest(stream, false, start, core::MediaTicks::fromMilliseconds(1'440'000), true);
  CHECK(request.item.value() == "item");
  CHECK(request.mediaSource.value() == "source");
  CHECK(request.url == "https://srv/videos/item/stream.mp4?ApiKey=t");
  CHECK(request.timeline.offset == start);
  CHECK(request.start == start);
  CHECK(request.runTime->milliseconds() == 1'440'000);
  CHECK(request.startPaused);
}

TEST_CASE("A reopen asks for the same source from the position with new tracks") {
  api::PlaybackInfoRequest previous;
  previous.userId = QStringLiteral("user");
  previous.maxStreamingBitrate = 8'000'000;
  previous.audioStreamIndex = 1;
  const auto stream = played(api::PlayMethod::Transcode, "https://srv/videos/item/stream.mp4");
  const auto request = reopenRequest(previous, stream, start, 4, -1);
  CHECK(request.itemId == QStringLiteral("item"));
  CHECK(request.mediaSourceId == QStringLiteral("source"));
  CHECK(request.userId == QStringLiteral("user"));
  CHECK(request.maxStreamingBitrate == 8'000'000);
  CHECK(request.startTimeTicks == start.ticks());
  CHECK(request.audioStreamIndex == 4);
  CHECK(request.subtitleStreamIndex == -1);
}
