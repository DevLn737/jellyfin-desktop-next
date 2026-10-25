#include "../support/DoctestQt.h"

#include <jfd/playback/PlayerOptions.h>

using namespace jfd::playback;

namespace {

  QByteArray valueOf(const QList<MpvProperty>& properties, const char* name) {
    for (const MpvProperty& property : properties)
      if (property.first == name) return property.second;
    FAIL("no property " << name);
    return {};
  }

} // namespace

TEST_CASE("Audio defaults are mpv's, so turning a setting back undoes it") {
  const QList<MpvProperty> properties = audioProperties({}, {QStringLiteral("auto")});
  CHECK(valueOf(properties, "audio-device") == "auto");
  CHECK(valueOf(properties, "audio-channels") == "auto-safe");
  CHECK(valueOf(properties, "audio-spdif") == "");
  CHECK(valueOf(properties, "audio-normalize-downmix") == "no");
  CHECK(valueOf(properties, "audio-exclusive") == "no");
}

TEST_CASE("Audio options map onto mpv; an unplugged device falls back to the default") {
  AudioOptions options;
  options.device = QStringLiteral("pulse/hdmi");
  options.channels = QStringLiteral("5.1,2.0");
  options.passthrough = {QStringLiteral("dts"), QStringLiteral("ac3"), QStringLiteral("dts-hd")};
  options.normalizeDownmix = true;
  QList<MpvProperty> properties =
      audioProperties(options, {QStringLiteral("auto"), QStringLiteral("pulse/hdmi")});
  CHECK(valueOf(properties, "audio-device") == "pulse/hdmi");
  CHECK(valueOf(properties, "audio-channels") == "5.1,2.0");
  // DTS-HD before DTS, as mpv needs.
  CHECK(valueOf(properties, "audio-spdif") == "ac3,dts-hd,dts");
  CHECK(valueOf(properties, "audio-normalize-downmix") == "yes");

  properties = audioProperties(options, {QStringLiteral("auto")});
  CHECK(valueOf(properties, "audio-device") == "auto");
}

TEST_CASE("Subtitle style defaults are mpv's") {
  const QList<MpvProperty> properties = subtitleProperties({});
  CHECK(valueOf(properties, "sub-font") == "sans-serif");
  CHECK(valueOf(properties, "sub-font-size") == "38");
  CHECK(valueOf(properties, "sub-color") == "#FFFFFFFF");
  CHECK(valueOf(properties, "sub-border-color") == "#FF000000");
  CHECK(valueOf(properties, "sub-border-size") == "3");
  CHECK(valueOf(properties, "sub-border-style") == "outline-and-shadow");
  CHECK(valueOf(properties, "sub-back-color") == "#00000000");
  CHECK(valueOf(properties, "sub-align-x") == "center");
  CHECK(valueOf(properties, "sub-pos") == "100");
  CHECK(valueOf(properties, "sub-ass-override") == "scale");
  CHECK(valueOf(properties, "sub-ass-style-overrides") == "ScaledBorderAndShadow=yes");
}

TEST_CASE("Subtitle style maps onto mpv, the background alpha before the colour") {
  SubtitleStyle style;
  style.font = QStringLiteral("serif");
  style.color = QStringLiteral("#FBF93E");
  style.borderColor = QStringLiteral("#FFFFFF");
  style.borderSize = 0;
  style.backgroundColor = QStringLiteral("#000000");
  style.backgroundAlpha = QStringLiteral("80");
  style.placement = QStringLiteral("left,top");
  style.size = 42;
  style.assOverride = QStringLiteral("force");
  style.assScaleBorderAndShadow = false;
  const QList<MpvProperty> properties = subtitleProperties(style);
  CHECK(valueOf(properties, "sub-font") == "serif");
  CHECK(valueOf(properties, "sub-font-size") == "42");
  CHECK(valueOf(properties, "sub-color") == "#FFFBF93E");
  CHECK(valueOf(properties, "sub-border-color") == "#FFFFFFFF");
  CHECK(valueOf(properties, "sub-border-size") == "0");
  CHECK(valueOf(properties, "sub-border-style") == "background-box");
  CHECK(valueOf(properties, "sub-back-color") == "#80000000");
  CHECK(valueOf(properties, "sub-align-x") == "left");
  CHECK(valueOf(properties, "sub-pos") == "10");
  CHECK(valueOf(properties, "sub-ass-override") == "force");
  CHECK(valueOf(properties, "sub-ass-style-overrides") == "ScaledBorderAndShadow=no");

  // A background colour alone is opaque.
  style.backgroundAlpha.clear();
  CHECK(valueOf(subtitleProperties(style), "sub-back-color") == "#FF000000");
}

TEST_CASE("The 2.x playback settings import with their meaning") {
  // As a 2.x profile stores them: choices as strings, sizes as numbers.
  const QVariantMap legacy{
      {QStringLiteral("video.hardwareDecoding"), QStringLiteral("copy")},
      {QStringLiteral("video.force_transcode_hevc"), true},
      {QStringLiteral("video.always_force_transcode"), false},
      {QStringLiteral("audio.devicetype"), QStringLiteral("hdmi")},
      {QStringLiteral("audio.channels"), QStringLiteral("auto")},
      {QStringLiteral("audio.device"), QStringLiteral("wasapi/{0.0.0.00000000}.{abc}")},
      {QStringLiteral("audio.normalize"), false},
      {QStringLiteral("audio.passthrough.ac3"), true},
      {QStringLiteral("audio.passthrough.truehd"), true},
      {QStringLiteral("subtitles.size"), 42},
      {QStringLiteral("subtitles.ass_style_override"), QStringLiteral("yes")},
      {QStringLiteral("subtitles.font"), QStringLiteral("sans-serif")},
      {QStringLiteral("subtitles.color"), QStringLiteral("#FBF93E")},
      {QStringLiteral("subtitles.border_size"), -1},
      {QStringLiteral("subtitles.background_transparency"), QStringLiteral("80")},
      {QStringLiteral("subtitles.placement"), QStringLiteral("center,top")},
      {QStringLiteral("subtitles.ass_scale_border_and_shadow"), false},
      // Not ours.
      {QStringLiteral("main.fullscreen"), true},
  };
  const QVariantMap imported = importLegacyPlayback(legacy);
  CHECK(imported.value(QStringLiteral("playback/hardwareDecoding")) == QStringLiteral("auto-copy-safe"));
  CHECK(imported.value(QStringLiteral("playback/forceTranscodeHevc")) == true);
  CHECK(imported.value(QStringLiteral("playback/alwaysTranscode")) == false);
  CHECK_FALSE(imported.contains(QStringLiteral("playback/forceTranscodeAv1")));
  CHECK(imported.value(QStringLiteral("audio/channels")) == QStringLiteral("auto-safe"));
  CHECK(imported.value(QStringLiteral("audio/device")) == QStringLiteral("wasapi/{0.0.0.00000000}.{abc}"));
  CHECK(imported.value(QStringLiteral("audio/normalizeDownmix")) == false);
  CHECK(imported.value(QStringLiteral("audio/passthroughAc3")) == true);
  CHECK(imported.value(QStringLiteral("audio/passthroughTruehd")) == true);
  CHECK(imported.value(QStringLiteral("audio/passthroughDts")) == false);
  CHECK(imported.value(QStringLiteral("subtitles/size")) == QStringLiteral("42"));
  CHECK(imported.value(QStringLiteral("subtitles/assOverride")) == QString());
  CHECK(imported.value(QStringLiteral("subtitles/font")) == QString());
  CHECK(imported.value(QStringLiteral("subtitles/color")) == QStringLiteral("#FBF93E"));
  CHECK(imported.value(QStringLiteral("subtitles/borderSize")) == QStringLiteral("-1"));
  CHECK(imported.value(QStringLiteral("subtitles/backgroundAlpha")) == QStringLiteral("80"));
  CHECK(imported.value(QStringLiteral("subtitles/placement")) == QStringLiteral("center,top"));
  CHECK(imported.value(QStringLiteral("subtitles/assScaleBorderAndShadow")) == false);
  for (const QString& key : imported.keys()) {
    INFO(key.toStdString());
    CHECK((key.startsWith(QLatin1String("playback/")) || key.startsWith(QLatin1String("audio/")) ||
           key.startsWith(QLatin1String("subtitles/"))));
  }
}

TEST_CASE("2.x passthrough applied only to an S/PDIF or HDMI device, as there") {
  QVariantMap legacy{{QStringLiteral("audio.passthrough.ac3"), true}, {QStringLiteral("audio.passthrough.eac3"), true}};
  // No device type stored: 2.x's default, basic, which passed nothing through.
  QVariantMap imported = importLegacyPlayback(legacy);
  CHECK(imported.value(QStringLiteral("audio/passthroughAc3")) == false);
  CHECK(imported.value(QStringLiteral("audio/passthroughEac3")) == false);

  // S/PDIF: AC3 and DTS only, in stereo.
  legacy.insert(QStringLiteral("audio.devicetype"), QStringLiteral("spdif"));
  legacy.insert(QStringLiteral("audio.channels"), QStringLiteral("7.1,5.1,2.0"));
  imported = importLegacyPlayback(legacy);
  CHECK(imported.value(QStringLiteral("audio/passthroughAc3")) == true);
  CHECK(imported.value(QStringLiteral("audio/passthroughEac3")) == false);
  CHECK(imported.value(QStringLiteral("audio/channels")) == QStringLiteral("2.0"));
}

TEST_CASE("Values this client has no choice for are left out") {
  const QVariantMap imported = importLegacyPlayback({{QStringLiteral("subtitles.font"), QStringLiteral("Comic Sans")},
                                                     {QStringLiteral("subtitles.color"), QStringLiteral("#123456")},
                                                     {QStringLiteral("video.hardwareDecoding"), QStringLiteral("odd")}});
  CHECK(imported.isEmpty());
}
