#include <jfd/offline/CompressedDownload.h>

#include <QCoreApplication>
#include <QJsonArray>

namespace jfd::offline {

  const QList<CompressedPreset>& compressedPresets() {
    static const QList<CompressedPreset> presets{
        {QStringLiteral("video-1080p"), QT_TRANSLATE_NOOP("DownloadSettings", "1080p HEVC, about 4 Mbit/s"),
         api::CompressedProfile{false, 1080, 4'000'000}},
        {QStringLiteral("video-720p"), QT_TRANSLATE_NOOP("DownloadSettings", "720p HEVC, about 2 Mbit/s"),
         api::CompressedProfile{false, 720, 2'000'000}},
        {QStringLiteral("audio-opus128"), QT_TRANSLATE_NOOP("DownloadSettings", "Opus, 128 kbit/s"),
         api::CompressedProfile{true, 0, 128'000}},
    };
    return presets;
  }

  std::optional<CompressedPreset> compressedPreset(const QString& id) {
    for (const CompressedPreset& preset : compressedPresets())
      if (preset.id == id) return preset;
    return std::nullopt;
  }

  std::optional<CompressedPreset> presetForQuality(const QString& quality, bool audio) {
    if (quality.isEmpty() || quality == OriginalQuality) return std::nullopt;
    if (audio) return compressedPreset(QStringLiteral("audio-opus128"));
    const auto preset = compressedPreset(quality);
    if (preset && !preset->profile.audio) return preset;
    return compressedPreset(QStringLiteral("video-1080p"));
  }

  QJsonObject compressedMediaSource(const QJsonObject& serverSource, const TranscodeState& transcode,
                                    std::int64_t size) {
    QJsonObject source = serverSource;
    const auto preset = compressedPreset(transcode.preset);
    const bool audioOnly = preset && preset->profile.audio;
    source.insert(QStringLiteral("Container"), QStringLiteral("mp4"));
    if (size > 0) source.insert(QStringLiteral("Size"), static_cast<double>(size));
    if (preset) source.insert(QStringLiteral("Bitrate"), static_cast<double>(preset->profile.bitrate));
    for (const char* key : {"Path", "TranscodingUrl", "TranscodingSubProtocol", "TranscodingContainer",
                            "DirectStreamUrl", "MediaAttachments", "DefaultSubtitleStreamIndex"})
      source.remove(QLatin1String(key));
    source.insert(QStringLiteral("SupportsDirectPlay"), true);
    source.insert(QStringLiteral("SupportsTranscoding"), false);

    QJsonArray streams;
    bool videoKept = false;
    int audioIndex = transcode.audioStreamIndex;
    for (const QJsonValue& value : serverSource.value(QLatin1String("MediaStreams")).toArray()) {
      QJsonObject stream = value.toObject();
      const QString type = stream.value(QLatin1String("Type")).toString();
      const int index = stream.value(QLatin1String("Index")).toInt(-1);
      stream.remove(QStringLiteral("DeliveryMethod"));
      stream.remove(QStringLiteral("DeliveryUrl"));
      stream.remove(QStringLiteral("Path"));
      if (type == QLatin1String("Video") && !videoKept && !audioOnly) {
        videoKept = true;
        if (!transcode.videoCodec.isEmpty()) stream.insert(QStringLiteral("Codec"), transcode.videoCodec);
        for (const char* key : {"BitRate", "Profile", "Level", "Width", "Height", "CodecTag", "IsAVC"})
          stream.remove(QLatin1String(key));
        stream.insert(QStringLiteral("IsExternal"), false);
        streams.append(stream);
      } else if (type == QLatin1String("Audio")) {
        // The chosen track, or the first one when the server's choice is
        // not known.
        if (audioIndex < 0) audioIndex = index;
        if (index != audioIndex) continue;
        if (!transcode.audioCodec.isEmpty()) stream.insert(QStringLiteral("Codec"), transcode.audioCodec);
        for (const char* key : {"BitRate", "Profile", "ChannelLayout", "Channels", "SampleRate", "CodecTag"})
          stream.remove(QLatin1String(key));
        stream.insert(QStringLiteral("IsExternal"), false);
        stream.insert(QStringLiteral("IsDefault"), true);
        streams.append(stream);
      } else if (type == QLatin1String("Subtitle")) {
        if (api::textSubtitleFormat(stream.value(QLatin1String("Codec")).toString()).isEmpty())
          continue;
        stream.insert(QStringLiteral("IsExternal"), true);
        stream.insert(QStringLiteral("DeliveryMethod"), QStringLiteral("External"));
        streams.append(stream);
      }
      // Fonts and other attachments are not in the copy.
    }
    source.insert(QStringLiteral("MediaStreams"), streams);
    if (audioIndex >= 0)
      source.insert(QStringLiteral("DefaultAudioStreamIndex"), audioIndex);
    else
      source.remove(QStringLiteral("DefaultAudioStreamIndex"));
    return source;
  }

} // namespace jfd::offline
