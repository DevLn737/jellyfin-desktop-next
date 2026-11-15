#pragma once

#include <jfd/api/Downloads.h>
#include <jfd/offline/Manifest.h>

#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

// The presets of a compressed copy (stage 3.10b) and what its manifest says
// about the file. Until the owner picks others: two for video, one for music.
namespace jfd::offline {

  struct CompressedPreset {
    // Stored in manifests and in the "downloads/quality" setting.
    QString id;
    // QT_TRANSLATE_NOOP("DownloadSettings", ...)
    const char* label = nullptr;
    api::CompressedProfile profile;
  };

  // "video-1080p" (HEVC, 1080 lines, ~4 Mbit/s), "video-720p" (HEVC, 720
  // lines, ~2 Mbit/s), "audio-opus128" (Opus, 128 kbit/s).
  const QList<CompressedPreset>& compressedPresets();
  std::optional<CompressedPreset> compressedPreset(const QString& id);

  // The "downloads/quality" values: "original" or a video preset.
  inline constexpr QLatin1String OriginalQuality{"original"};

  // The preset a download of this kind gets for a quality setting: none for
  // "original", the music preset for audio whatever video preset is set,
  // the video preset (or the 1080p one, for an unknown value) for video.
  std::optional<CompressedPreset> presetForQuality(const QString& quality, bool audio);

  // The media source of a finished compressed copy, from the server's
  // source it was made from: the container is mp4, `size` and the preset's
  // bit rate; the streams are the video (if any) and the one audio track
  // inside the file, in that order, with the codecs the server encoded
  // to, and the text subtitles (embedded or not) as external files, which
  // ExtrasDownloader fetches by their index. Picture subtitles are left
  // out. Indexes stay the server's, so the watch journal and the track menu
  // name the same tracks.
  QJsonObject compressedMediaSource(const QJsonObject& serverSource, const TranscodeState& transcode,
                                    std::int64_t size);

} // namespace jfd::offline
