#include <jfd/api/DeviceProfile.h>

#include <QJsonArray>

namespace jfd::api {

  namespace {
    QJsonObject condition(const char* condition, const char* property, const char* value) {
      return QJsonObject{{QStringLiteral("Condition"), QLatin1String(condition)},
                         {QStringLiteral("Property"), QLatin1String(property)},
                         {QStringLiteral("Value"), QLatin1String(value)}};
    }

    QJsonObject videoProfile(const QJsonArray& conditions, const char* codec = nullptr) {
      QJsonObject profile{{QStringLiteral("Type"), QStringLiteral("Video")},
                          {QStringLiteral("Conditions"), conditions}};
      if (codec) profile.insert(QStringLiteral("Codec"), QLatin1String(codec));
      return profile;
    }

    QJsonObject typeOnly(const char* type) {
      return QJsonObject{{QStringLiteral("Type"), QLatin1String(type)}};
    }

    QJsonObject subtitle(const char* format, const char* method) {
      return QJsonObject{{QStringLiteral("Format"), QLatin1String(format)},
                         {QStringLiteral("Method"), QLatin1String(method)}};
    }
  } // namespace

  QJsonObject buildDeviceProfile(const DeviceProfileOptions& options) {
    QJsonArray codecProfiles;
    if (options.forceTranscodeDolbyVision)
      codecProfiles.append(
          videoProfile(QJsonArray{condition("NotEquals", "VideoRangeType", "DOVI")}));
    if (options.forceTranscodeHdr)
      codecProfiles.append(videoProfile(QJsonArray{condition("Equals", "VideoRangeType", "SDR")}));
    if (options.forceTranscodeHi10p)
      codecProfiles.append(
          videoProfile(QJsonArray{condition("LessThanEqual", "VideoBitDepth", "8")}));
    // "Width equals 0" never holds, so the codec is never played directly.
    if (options.forceTranscodeHevc) {
      codecProfiles.append(videoProfile(QJsonArray{condition("Equals", "Width", "0")}, "hevc"));
      codecProfiles.append(videoProfile(QJsonArray{condition("Equals", "Width", "0")}, "h265"));
    }
    if (options.forceTranscodeAv1)
      codecProfiles.append(videoProfile(QJsonArray{condition("Equals", "Width", "0")}, "av1"));
    if (options.forceTranscode4k)
      codecProfiles.append(videoProfile(QJsonArray{condition("LessThanEqual", "Width", "1920"),
                                                   condition("LessThanEqual", "Height", "1080")}));

    QJsonArray directPlay{typeOnly("Audio"), typeOnly("Photo")};
    if (!options.alwaysForceTranscode) directPlay.append(typeOnly("Video"));

    const char* videoCodecs = !options.allowTranscodeToHevc   ? "h264,mpeg4,mpeg2video"
                              : options.preferTranscodeToHevc ? "h265,hevc,h264,mpeg4,mpeg2video"
                                                              : "h264,h265,hevc,mpeg4,mpeg2video";
    const QJsonObject videoTranscode{
        {QStringLiteral("Container"), QStringLiteral("ts")},
        {QStringLiteral("Type"), QStringLiteral("Video")},
        {QStringLiteral("Protocol"), QStringLiteral("hls")},
        {QStringLiteral("AudioCodec"), QStringLiteral("aac,mp3,ac3,opus,vorbis")},
        {QStringLiteral("VideoCodec"), QLatin1String(videoCodecs)},
        {QStringLiteral("MaxAudioChannels"),
         QString::number(options.maxTranscodeAudioChannels <= 2 ? 2 : 6)}};

    const QJsonArray transcoding{typeOnly("Audio"), videoTranscode,
                                 QJsonObject{{QStringLiteral("Container"), QStringLiteral("jpeg")},
                                             {QStringLiteral("Type"), QStringLiteral("Photo")}}};

    const QJsonArray subtitles{
        subtitle("srt", "External"), subtitle("srt", "Embed"),    subtitle("ass", "External"),
        subtitle("ass", "Embed"),    subtitle("sub", "Embed"),    subtitle("sub", "External"),
        subtitle("ssa", "Embed"),    subtitle("ssa", "External"), subtitle("smi", "Embed"),
        subtitle("smi", "External"), subtitle("pgssub", "Embed"), subtitle("dvdsub", "Embed"),
        subtitle("dvbsub", "Embed"), subtitle("pgs", "Embed")};

    return QJsonObject{{QStringLiteral("Name"), QStringLiteral("Jellyfin Desktop Next")},
                       {QStringLiteral("MaxStaticBitrate"), 1'000'000'000},
                       {QStringLiteral("MusicStreamingTranscodingBitrate"), 1'280'000},
                       {QStringLiteral("TimelineOffsetSeconds"), 5},
                       {QStringLiteral("TranscodingProfiles"), transcoding},
                       {QStringLiteral("DirectPlayProfiles"), directPlay},
                       {QStringLiteral("ResponseProfiles"), QJsonArray{}},
                       {QStringLiteral("ContainerProfiles"), QJsonArray{}},
                       {QStringLiteral("CodecProfiles"), codecProfiles},
                       {QStringLiteral("SubtitleProfiles"), subtitles}};
  }

} // namespace jfd::api
