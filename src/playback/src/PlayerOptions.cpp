#include <jfd/playback/PlayerOptions.h>

namespace jfd::playback {

  namespace {
    QByteArray yesNo(bool value) { return value ? QByteArrayLiteral("yes") : QByteArrayLiteral("no"); }

    // mpv's defaults, for settings left at "default".
    constexpr int MpvFontSize = 38;
    constexpr int MpvBorderSize = 3;

    QByteArray colorOr(const QString& color, const QString& alpha, const char* fallback) {
      if (color.size() != 7 || !color.startsWith(QLatin1Char('#'))) return QByteArray(fallback);
      return (QLatin1Char('#') + (alpha.size() == 2 ? alpha : QStringLiteral("FF")) + color.mid(1)).toLatin1();
    }

    // 2.x stored choices as strings, numbers as numbers; either may come back.
    QString text(const QVariant& value) { return value.toString(); }
  } // namespace

  const QStringList& passthroughCodecs() {
    // dts-hd includes dts, and mpv takes the first that matches, so dts-hd
    // comes before dts.
    static const QStringList codecs{QStringLiteral("ac3"), QStringLiteral("eac3"), QStringLiteral("dts-hd"),
                                    QStringLiteral("dts"), QStringLiteral("truehd")};
    return codecs;
  }

  QList<MpvProperty> audioProperties(const AudioOptions& options, const QStringList& availableDevices) {
    const QString device =
        options.device.isEmpty() || !availableDevices.contains(options.device) ? QStringLiteral("auto") : options.device;
    QStringList spdif;
    for (const QString& codec : passthroughCodecs())
      if (options.passthrough.contains(codec)) spdif.append(codec);
    return {{QByteArrayLiteral("audio-device"), device.toUtf8()},
            {QByteArrayLiteral("audio-channels"),
             options.channels.isEmpty() ? QByteArrayLiteral("auto-safe") : options.channels.toLatin1()},
            {QByteArrayLiteral("audio-spdif"), spdif.join(QLatin1Char(',')).toLatin1()},
            {QByteArrayLiteral("audio-normalize-downmix"), yesNo(options.normalizeDownmix)},
            {QByteArrayLiteral("audio-exclusive"), yesNo(options.exclusive)}};
  }

  QList<MpvProperty> subtitleProperties(const SubtitleStyle& style) {
    QByteArray alignX = QByteArrayLiteral("center");
    int position = 100;
    const QStringList placement = style.placement.split(QLatin1Char(','));
    if (placement.size() == 2) {
      alignX = placement[0].toLatin1();
      position = placement[1] == QLatin1String("top") ? 10 : 100;
    }
    return {
        {QByteArrayLiteral("sub-font"), style.font.isEmpty() ? QByteArrayLiteral("sans-serif") : style.font.toUtf8()},
        {QByteArrayLiteral("sub-font-size"), QByteArray::number(style.size > 0 ? style.size : MpvFontSize)},
        {QByteArrayLiteral("sub-color"), colorOr(style.color, {}, "#FFFFFFFF")},
        {QByteArrayLiteral("sub-border-color"), colorOr(style.borderColor, {}, "#FF000000")},
        {QByteArrayLiteral("sub-border-size"), QByteArray::number(style.borderSize >= 0 ? style.borderSize : MpvBorderSize)},
        // mpv draws the background only with a box border style.
        {QByteArrayLiteral("sub-border-style"),
         style.backgroundColor.isEmpty() ? QByteArrayLiteral("outline-and-shadow") : QByteArrayLiteral("background-box")},
        {QByteArrayLiteral("sub-back-color"), colorOr(style.backgroundColor, style.backgroundAlpha, "#00000000")},
        {QByteArrayLiteral("sub-align-x"), alignX},
        {QByteArrayLiteral("sub-pos"), QByteArray::number(position)},
        {QByteArrayLiteral("sub-ass-override"),
         style.assOverride.isEmpty() ? QByteArrayLiteral("scale") : style.assOverride.toLatin1()},
        {QByteArrayLiteral("sub-ass-style-overrides"), style.assScaleBorderAndShadow
                                                           ? QByteArrayLiteral("ScaledBorderAndShadow=yes")
                                                           : QByteArrayLiteral("ScaledBorderAndShadow=no")},
    };
  }

  QVariantMap importLegacyPlayback(const QVariantMap& legacy) {
    QVariantMap result;
    const auto copyBool = [&](const char* from, const QString& to) {
      const QString key = QLatin1String(from);
      if (legacy.contains(key)) result.insert(to, legacy.value(key).toBool());
    };
    // A string choice, kept when it is one of ours; mapped otherwise.
    const auto copyChoice = [&](const char* from, const QString& to, const QHash<QString, QString>& map,
                                const QStringList& same) {
      const QString key = QLatin1String(from);
      if (!legacy.contains(key)) return;
      const QString value = text(legacy.value(key));
      if (map.contains(value)) result.insert(to, map.value(value));
      else if (same.contains(value)) result.insert(to, value);
    };

    copyChoice("video.hardwareDecoding", QStringLiteral("playback/hardwareDecoding"),
               {{QStringLiteral("enabled"), QStringLiteral("auto-safe")},
                {QStringLiteral("osx_compat"), QStringLiteral("auto-safe")},
                {QStringLiteral("copy"), QStringLiteral("auto-copy-safe")},
                {QStringLiteral("disabled"), QStringLiteral("no")}},
               {});
    for (const auto& [from, to] : {std::pair{"video.always_force_transcode", "alwaysTranscode"},
                                   std::pair{"video.force_transcode_hevc", "forceTranscodeHevc"},
                                   std::pair{"video.force_transcode_av1", "forceTranscodeAv1"},
                                   std::pair{"video.force_transcode_4k", "forceTranscode4k"},
                                   std::pair{"video.force_transcode_hdr", "forceTranscodeHdr"},
                                   std::pair{"video.force_transcode_hi10p", "forceTranscodeHi10p"},
                                   std::pair{"video.force_transcode_dovi", "forceTranscodeDolbyVision"},
                                   std::pair{"video.allow_transcode_to_hevc", "allowTranscodeToHevc"},
                                   std::pair{"video.prefer_transcode_to_h265", "preferTranscodeToHevc"}})
      copyBool(from, QStringLiteral("playback/") + QLatin1String(to));

    // Audio. 2.x passed codecs through only for an S/PDIF or HDMI device,
    // and S/PDIF only for AC3 and DTS in stereo.
    if (legacy.contains(QStringLiteral("audio.device")))
      result.insert(QStringLiteral("audio/device"), text(legacy.value(QStringLiteral("audio.device"))));
    copyChoice("audio.channels", QStringLiteral("audio/channels"), {{QStringLiteral("auto"), QStringLiteral("auto-safe")}},
               {QStringLiteral("2.0"), QStringLiteral("5.1,2.0"), QStringLiteral("7.1,5.1,2.0")});
    copyBool("audio.normalize", QStringLiteral("audio/normalizeDownmix"));
    copyBool("audio.exclusive", QStringLiteral("audio/exclusive"));
    const QString deviceType = text(legacy.value(QStringLiteral("audio.devicetype"), QStringLiteral("basic")));
    const QStringList spdifCodecs{QStringLiteral("ac3"), QStringLiteral("dts")};
    for (const auto& [codec, key] : {std::pair{"ac3", "passthroughAc3"}, std::pair{"eac3", "passthroughEac3"},
                                     std::pair{"dts", "passthroughDts"}, std::pair{"dts-hd", "passthroughDtsHd"},
                                     std::pair{"truehd", "passthroughTruehd"}}) {
      const QString legacyKey = QStringLiteral("audio.passthrough.") + QLatin1String(codec);
      if (!legacy.contains(legacyKey) && !legacy.contains(QStringLiteral("audio.devicetype"))) continue;
      const bool allowed = deviceType == QLatin1String("hdmi") ||
                           (deviceType == QLatin1String("spdif") && spdifCodecs.contains(QLatin1String(codec)));
      result.insert(QStringLiteral("audio/") + QLatin1String(key), allowed && legacy.value(legacyKey).toBool());
    }
    if (deviceType == QLatin1String("spdif")) result.insert(QStringLiteral("audio/channels"), QStringLiteral("2.0"));

    // Subtitles.
    copyChoice("subtitles.size", QStringLiteral("subtitles/size"), {},
               {QStringLiteral("-1"), QStringLiteral("18"), QStringLiteral("24"), QStringLiteral("32"),
                QStringLiteral("42"), QStringLiteral("60")});
    copyChoice("subtitles.ass_style_override", QStringLiteral("subtitles/assOverride"),
               {{QStringLiteral("yes"), QString()}, {QStringLiteral("scale"), QString()}},
               {QString(), QStringLiteral("no"), QStringLiteral("force"), QStringLiteral("strip")});
    copyBool("subtitles.ass_scale_border_and_shadow", QStringLiteral("subtitles/assScaleBorderAndShadow"));
    copyChoice("subtitles.font", QStringLiteral("subtitles/font"), {{QStringLiteral("sans-serif"), QString()}},
               {QString(), QStringLiteral("serif"), QStringLiteral("monospace"), QStringLiteral("script"),
                QStringLiteral("display"), QStringLiteral("cursive"), QStringLiteral("fantasy")});
    const QStringList colors{QString(),           QStringLiteral("#000000"), QStringLiteral("#FFFFFF"),
                             QStringLiteral("#EEEEEE"), QStringLiteral("#CCCCCC"), QStringLiteral("#FBF93E"),
                             QStringLiteral("#FFFFCC"), QStringLiteral("#EEEDB8")};
    copyChoice("subtitles.color", QStringLiteral("subtitles/color"), {}, colors);
    copyChoice("subtitles.border_color", QStringLiteral("subtitles/borderColor"), {}, colors);
    copyChoice("subtitles.background_color", QStringLiteral("subtitles/backgroundColor"), {}, colors);
    copyChoice("subtitles.border_size", QStringLiteral("subtitles/borderSize"), {},
               {QStringLiteral("-1"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"),
                QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5")});
    copyChoice("subtitles.background_transparency", QStringLiteral("subtitles/backgroundAlpha"), {},
               {QString(), QStringLiteral("FF"), QStringLiteral("C0"), QStringLiteral("80"), QStringLiteral("40"),
                QStringLiteral("00")});
    copyChoice("subtitles.placement", QStringLiteral("subtitles/placement"), {},
               {QString(), QStringLiteral("left,bottom"), QStringLiteral("right,bottom"),
                QStringLiteral("center,bottom"), QStringLiteral("left,top"), QStringLiteral("right,top"),
                QStringLiteral("center,top")});
    return result;
  }

} // namespace jfd::playback
