#include "AppServices.h"

#include <jfd/playback/PlayerOptions.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/HdrOutput.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/PlayerModel.h>
#include <jfd/ui/PlayerSession.h>

#include <MpvPlayer.h>

#include <QCoreApplication>
#include <QStringList>
#include <QUrl>

#include <deque>
#include <iterator>

namespace jfd::desktop {

  namespace {
    const QString AutoSkipKey = QStringLiteral("playback/autoSkipSegments");
    const QString HdrKey = QStringLiteral("playback/hdr10");
    const QString VolumeKey = QStringLiteral("playback/defaultVolume");
    const QString AutoNextKey = QStringLiteral("playback/autoNextEpisode");
    const QString HwdecKey = QStringLiteral("playback/hardwareDecoding");
    const QString AudioLanguageKey = QStringLiteral("audio/language");
    const QString SubtitleLanguageKey = QStringLiteral("subtitles/language");
    const QString SubtitleModeKey = QStringLiteral("subtitles/mode");
    const QString AudioSection = QStringLiteral("audio/");
    const QString SubtitleSection = QStringLiteral("subtitles/");

    // ISO 639-2/B codes, as Jellyfin names stream languages. The labels are
    // listed per section: each section translates in its own context.
    const char* const AudioLanguageLabels[] = {
        QT_TRANSLATE_NOOP("AudioSettings", "As on the server"),
        QT_TRANSLATE_NOOP("AudioSettings", "Russian"),
        QT_TRANSLATE_NOOP("AudioSettings", "English"),
        QT_TRANSLATE_NOOP("AudioSettings", "Japanese"),
        QT_TRANSLATE_NOOP("AudioSettings", "Ukrainian"),
        QT_TRANSLATE_NOOP("AudioSettings", "German"),
        QT_TRANSLATE_NOOP("AudioSettings", "French"),
        QT_TRANSLATE_NOOP("AudioSettings", "Spanish"),
        QT_TRANSLATE_NOOP("AudioSettings", "Italian"),
        QT_TRANSLATE_NOOP("AudioSettings", "Korean"),
        QT_TRANSLATE_NOOP("AudioSettings", "Chinese"),
    };
    const char* const SubtitleLanguageLabels[] = {
        QT_TRANSLATE_NOOP("SubtitleSettings", "As on the server"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "Russian"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "English"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "Japanese"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "Ukrainian"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "German"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "French"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "Spanish"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "Italian"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "Korean"),
        QT_TRANSLATE_NOOP("SubtitleSettings", "Chinese"),
    };

    QList<settings::Choice> languageChoices(const char* const (&labels)[11]) {
      const char* codes[] = {"", "rus", "eng", "jpn", "ukr", "ger", "fre", "spa", "ita", "kor", "chi"};
      QList<settings::Choice> choices;
      for (std::size_t i = 0; i < std::size(codes); ++i) choices.append({QLatin1String(codes[i]), labels[i]});
      return choices;
    }

    const QString MaxBitrateKey = QStringLiteral("playback/maxBitrate");

    // The transcoding settings: key inside the section, the 2.x key, the
    // DeviceProfileOptions field, labels.
    struct TranscodeSetting {
      const char* key;
      const char* legacyKey;
      bool api::DeviceProfileOptions::*field;
      const char* label;
      const char* description;
    };
    const TranscodeSetting TranscodeSettings[] = {
        {"alwaysTranscode", "video.always_force_transcode", &api::DeviceProfileOptions::alwaysForceTranscode,
         QT_TRANSLATE_NOOP("PlayerSettings", "Always transcode video"),
         QT_TRANSLATE_NOOP("PlayerSettings", "The server converts every video, even ones the player could play as they are.")},
        {"forceTranscodeHevc", "video.force_transcode_hevc", &api::DeviceProfileOptions::forceTranscodeHevc,
         QT_TRANSLATE_NOOP("PlayerSettings", "Transcode HEVC"), nullptr},
        {"forceTranscodeAv1", "video.force_transcode_av1", &api::DeviceProfileOptions::forceTranscodeAv1,
         QT_TRANSLATE_NOOP("PlayerSettings", "Transcode AV1"), nullptr},
        {"forceTranscode4k", "video.force_transcode_4k", &api::DeviceProfileOptions::forceTranscode4k,
         QT_TRANSLATE_NOOP("PlayerSettings", "Transcode 4K"), nullptr},
        {"forceTranscodeHdr", "video.force_transcode_hdr", &api::DeviceProfileOptions::forceTranscodeHdr,
         QT_TRANSLATE_NOOP("PlayerSettings", "Transcode HDR"), nullptr},
        {"forceTranscodeHi10p", "video.force_transcode_hi10p", &api::DeviceProfileOptions::forceTranscodeHi10p,
         QT_TRANSLATE_NOOP("PlayerSettings", "Transcode 10-bit H.264"), nullptr},
        {"forceTranscodeDolbyVision", "video.force_transcode_dovi",
         &api::DeviceProfileOptions::forceTranscodeDolbyVision,
         QT_TRANSLATE_NOOP("PlayerSettings", "Transcode Dolby Vision"), nullptr},
        {"allowTranscodeToHevc", "video.allow_transcode_to_hevc", &api::DeviceProfileOptions::allowTranscodeToHevc,
         QT_TRANSLATE_NOOP("PlayerSettings", "Allow transcoding to HEVC"),
         QT_TRANSLATE_NOOP("PlayerSettings", "Smaller streams at the same quality, if the server's hardware can encode HEVC.")},
        {"preferTranscodeToHevc", "video.prefer_transcode_to_h265", &api::DeviceProfileOptions::preferTranscodeToHevc,
         QT_TRANSLATE_NOOP("PlayerSettings", "Prefer HEVC when transcoding"), nullptr},
    };

    api::DeviceProfileOptions profileOptions(const settings::Settings& settings) {
      api::DeviceProfileOptions options;
      for (const TranscodeSetting& setting : TranscodeSettings)
        options.*setting.field = settings.value(QStringLiteral("playback/") + QLatin1String(setting.key)).toBool();
      return options;
    }

    bool isTranscodeKey(const QString& key) {
      for (const TranscodeSetting& setting : TranscodeSettings)
        if (key == QStringLiteral("playback/") + QLatin1String(setting.key)) return true;
      return false;
    }

    playback::TrackDefaults trackDefaults(const settings::Settings& settings) {
      playback::TrackDefaults defaults;
      defaults.audioLanguage = settings.value(AudioLanguageKey).toString();
      defaults.subtitleLanguage = settings.value(SubtitleLanguageKey).toString();
      const QString mode = settings.value(SubtitleModeKey).toString();
      defaults.subtitleMode = mode == QLatin1String("always")   ? playback::SubtitleMode::Always
                              : mode == QLatin1String("forced") ? playback::SubtitleMode::OnlyForced
                              : mode == QLatin1String("off")    ? playback::SubtitleMode::Off
                                                                : playback::SubtitleMode::Default;
      return defaults;
    }

    // The passthrough settings: key inside the audio section, mpv's codec.
    const std::pair<const char*, const char*> PassthroughSettings[] = {
        {"passthroughAc3", "ac3"},  {"passthroughEac3", "eac3"},     {"passthroughDts", "dts"},
        {"passthroughDtsHd", "dts-hd"}, {"passthroughTruehd", "truehd"}};

    playback::AudioOptions audioOptions(const settings::Settings& settings) {
      playback::AudioOptions options;
      options.device = settings.value(QStringLiteral("audio/device")).toString();
      options.channels = settings.value(QStringLiteral("audio/channels")).toString();
      for (const auto& [key, codec] : PassthroughSettings)
        if (settings.value(AudioSection + QLatin1String(key)).toBool()) options.passthrough.append(QLatin1String(codec));
      options.normalizeDownmix = settings.value(QStringLiteral("audio/normalizeDownmix")).toBool();
      options.exclusive = settings.value(QStringLiteral("audio/exclusive")).toBool();
      return options;
    }

    playback::SubtitleStyle subtitleStyle(const settings::Settings& settings) {
      const auto value = [&](const char* key) { return settings.value(SubtitleSection + QLatin1String(key)); };
      playback::SubtitleStyle style;
      style.font = value("font").toString();
      style.color = value("color").toString();
      style.borderColor = value("borderColor").toString();
      style.borderSize = value("borderSize").toString().toInt();
      style.backgroundColor = value("backgroundColor").toString();
      style.backgroundAlpha = value("backgroundAlpha").toString();
      style.placement = value("placement").toString();
      style.size = value("size").toString().toInt();
      style.assOverride = value("assOverride").toString();
      style.assScaleBorderAndShadow = value("assScaleBorderAndShadow").toBool();
      return style;
    }

    void applyAudio(media::MpvPlayer& player, const settings::Settings& settings) {
      QStringList devices;
      for (const QVariant& device : player.property("audio-device-list").toList())
        devices.append(device.toMap().value(QStringLiteral("name")).toString());
      player.setProperties(playback::audioProperties(audioOptions(settings), devices));
    }

    void applySubtitles(media::MpvPlayer& player, const settings::Settings& settings) {
      player.setProperties(playback::subtitleProperties(subtitleStyle(settings)));
    }

    // Labels for the audio devices found at start. The registry keeps label
    // pointers, so they live as long as the program; a label without a
    // translation is shown as is.
    std::deque<QByteArray>& deviceLabels() {
      static std::deque<QByteArray> labels;
      return labels;
    }

    settings::SettingDescriptor choiceSetting(const char* key, const QString& defaultValue, const char* label,
                                              QList<settings::Choice> choices, const char* legacyKey = nullptr) {
      settings::SettingDescriptor setting;
      setting.key = QLatin1String(key);
      setting.type = settings::SettingType::Choice;
      setting.defaultValue = defaultValue;
      setting.label = label;
      setting.choices = std::move(choices);
      if (legacyKey != nullptr) setting.legacyKey = QLatin1String(legacyKey);
      return setting;
    }

    settings::SettingDescriptor boolSetting(const char* key, bool defaultValue, const char* label,
                                            const char* legacyKey = nullptr) {
      settings::SettingDescriptor setting;
      setting.key = QLatin1String(key);
      setting.type = settings::SettingType::Bool;
      setting.defaultValue = defaultValue;
      setting.label = label;
      if (legacyKey != nullptr) setting.legacyKey = QLatin1String(legacyKey);
      return setting;
    }

    // The 2.x client's color choices.
    QList<settings::Choice> colorChoices(const char* defaultLabel) {
      return {{QString(), defaultLabel},
              {QStringLiteral("#FFFFFF"), QT_TRANSLATE_NOOP("SubtitleSettings", "White")},
              {QStringLiteral("#EEEEEE"), QT_TRANSLATE_NOOP("SubtitleSettings", "Light grey")},
              {QStringLiteral("#CCCCCC"), QT_TRANSLATE_NOOP("SubtitleSettings", "Grey")},
              {QStringLiteral("#FBF93E"), QT_TRANSLATE_NOOP("SubtitleSettings", "Yellow")},
              {QStringLiteral("#FFFFCC"), QT_TRANSLATE_NOOP("SubtitleSettings", "Light yellow")},
              {QStringLiteral("#EEEDB8"), QT_TRANSLATE_NOOP("SubtitleSettings", "Pale yellow")},
              {QStringLiteral("#000000"), QT_TRANSLATE_NOOP("SubtitleSettings", "Black")}};
    }

    bool registerAudioSettings(settings::SettingsRegistry& registry) {
      settings::SectionDescriptor section;
      section.id = QStringLiteral("audio");
      section.context = "AudioSettings";
      section.title = QT_TRANSLATE_NOOP("AudioSettings", "Sound");
      section.order = 102;

      settings::SettingDescriptor language = choiceSetting(
          "language", QString(), QT_TRANSLATE_NOOP("AudioSettings", "Audio language"), languageChoices(AudioLanguageLabels));
      language.description = QT_TRANSLATE_NOOP(
          "AudioSettings", "Until you pick a track yourself; your pick then carries to the next episodes.");
      section.settings.append(language);

      // mpv lists the outputs once, at start; a device plugged in later
      // shows after a restart.
      QList<settings::Choice> devices{{QStringLiteral("auto"), QT_TRANSLATE_NOOP("AudioSettings", "System default")}};
      for (const media::MpvPlayer::AudioDevice& device : media::MpvPlayer::listAudioDevices()) {
        if (device.name == QLatin1String("auto")) continue;
        deviceLabels().push_back(device.description.isEmpty() ? device.name.toUtf8() : device.description.toUtf8());
        devices.append({device.name, deviceLabels().back().constData()});
      }
      settings::SettingDescriptor device = choiceSetting("device", QStringLiteral("auto"),
                                                         QT_TRANSLATE_NOOP("AudioSettings", "Output device"),
                                                         std::move(devices), "audio.device");
      device.description = QT_TRANSLATE_NOOP(
          "AudioSettings", "If the device is unplugged, sound plays on the system default. A new device shows after a restart.");
      section.settings.append(device);

      // mpv's audio-channels; the 2.x client's values.
      settings::SettingDescriptor channels = choiceSetting(
          "channels", QStringLiteral("auto-safe"), QT_TRANSLATE_NOOP("AudioSettings", "Speakers"),
          {{QStringLiteral("auto-safe"), QT_TRANSLATE_NOOP("AudioSettings", "As the device reports")},
           {QStringLiteral("2.0"), QT_TRANSLATE_NOOP("AudioSettings", "Stereo")},
           {QStringLiteral("5.1,2.0"), QT_TRANSLATE_NOOP("AudioSettings", "5.1")},
           {QStringLiteral("7.1,5.1,2.0"), QT_TRANSLATE_NOOP("AudioSettings", "7.1")}},
          "audio.channels");
      channels.description = QT_TRANSLATE_NOOP(
          "AudioSettings", "More channels than the speakers have are mixed down.");
      section.settings.append(channels);

      settings::SettingDescriptor normalize = boolSetting(
          "normalizeDownmix", false, QT_TRANSLATE_NOOP("AudioSettings", "Even out mixed-down sound"), "audio.normalize");
      normalize.description = QT_TRANSLATE_NOOP(
          "AudioSettings", "Quieter, but nothing clips when 5.1 or 7.1 plays on fewer speakers.");
      section.settings.append(normalize);

      const char* passthroughLabels[] = {
          QT_TRANSLATE_NOOP("AudioSettings", "Pass through Dolby Digital (AC3)"),
          QT_TRANSLATE_NOOP("AudioSettings", "Pass through Dolby Digital Plus (E-AC3)"),
          QT_TRANSLATE_NOOP("AudioSettings", "Pass through DTS"),
          QT_TRANSLATE_NOOP("AudioSettings", "Pass through DTS-HD"),
          QT_TRANSLATE_NOOP("AudioSettings", "Pass through Dolby TrueHD")};
      const char* passthroughLegacy[] = {"audio.passthrough.ac3", "audio.passthrough.eac3", "audio.passthrough.dts",
                                         "audio.passthrough.dts-hd", "audio.passthrough.truehd"};
      for (std::size_t i = 0; i < std::size(PassthroughSettings); ++i) {
        settings::SettingDescriptor passthrough =
            boolSetting(PassthroughSettings[i].first, false, passthroughLabels[i], passthroughLegacy[i]);
        if (i == 0)
          passthrough.description = QT_TRANSLATE_NOOP(
              "AudioSettings", "Only with a receiver or TV that decodes it, over HDMI or S/PDIF. Otherwise there is no sound.");
        section.settings.append(passthrough);
      }

      // WASAPI exclusive mode; other systems have none.
      settings::SettingDescriptor exclusive = boolSetting(
          "exclusive", false, QT_TRANSLATE_NOOP("AudioSettings", "Exclusive mode"), "audio.exclusive");
      exclusive.description = QT_TRANSLATE_NOOP(
          "AudioSettings", "The player takes the device for itself: other programs go silent while a video plays.");
#ifndef Q_OS_WIN
      exclusive.hidden = true;
#endif
      section.settings.append(exclusive);
      return registry.registerSection(std::move(section));
    }

    bool registerSubtitleSettings(settings::SettingsRegistry& registry) {
      settings::SectionDescriptor section;
      section.id = QStringLiteral("subtitles");
      section.context = "SubtitleSettings";
      section.title = QT_TRANSLATE_NOOP("SubtitleSettings", "Subtitles");
      section.order = 104;

      section.settings.append(choiceSetting(
          "mode", QStringLiteral("default"), QT_TRANSLATE_NOOP("SubtitleSettings", "Show subtitles"),
          {{QStringLiteral("default"), QT_TRANSLATE_NOOP("SubtitleSettings", "As on the server")},
           {QStringLiteral("always"), QT_TRANSLATE_NOOP("SubtitleSettings", "Always, in the subtitle language")},
           {QStringLiteral("forced"), QT_TRANSLATE_NOOP("SubtitleSettings", "Only forced (signs and songs)")},
           {QStringLiteral("off"), QT_TRANSLATE_NOOP("SubtitleSettings", "Off")}}));
      section.settings.append(choiceSetting("language", QString(),
                                            QT_TRANSLATE_NOOP("SubtitleSettings", "Subtitle language"),
                                            languageChoices(SubtitleLanguageLabels)));

      // The 2.x client's sizes (mpv sub-font-size), so its value imports as is.
      settings::SettingDescriptor size = choiceSetting(
          "size", QStringLiteral("-1"), QT_TRANSLATE_NOOP("SubtitleSettings", "Size"),
          {{QStringLiteral("-1"), QT_TRANSLATE_NOOP("SubtitleSettings", "Default")},
           {QStringLiteral("18"), QT_TRANSLATE_NOOP("SubtitleSettings", "Tiny")},
           {QStringLiteral("24"), QT_TRANSLATE_NOOP("SubtitleSettings", "Small")},
           {QStringLiteral("32"), QT_TRANSLATE_NOOP("SubtitleSettings", "Normal")},
           {QStringLiteral("42"), QT_TRANSLATE_NOOP("SubtitleSettings", "Large")},
           {QStringLiteral("60"), QT_TRANSLATE_NOOP("SubtitleSettings", "Huge")}},
          "subtitles.size");
      size.description = QT_TRANSLATE_NOOP(
          "SubtitleSettings", "This and the style below apply to plain subtitles (SRT, VTT). Styled ones (ASS) follow the "
                              "last setting.");
      section.settings.append(size);

      // Generic names fontconfig and Windows resolve to an installed font.
      section.settings.append(choiceSetting(
          "font", QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Font"),
          {{QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Default (sans serif)")},
           {QStringLiteral("serif"), QT_TRANSLATE_NOOP("SubtitleSettings", "Serif")},
           {QStringLiteral("monospace"), QT_TRANSLATE_NOOP("SubtitleSettings", "Monospace")}},
          "subtitles.font"));
      section.settings.append(choiceSetting("color", QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Text colour"),
                                            colorChoices(QT_TRANSLATE_NOOP("SubtitleSettings", "Default (white)")),
                                            "subtitles.color"));
      section.settings.append(choiceSetting(
          "borderSize", QStringLiteral("-1"), QT_TRANSLATE_NOOP("SubtitleSettings", "Outline"),
          {{QStringLiteral("-1"), QT_TRANSLATE_NOOP("SubtitleSettings", "Default")},
           {QStringLiteral("0"), QT_TRANSLATE_NOOP("SubtitleSettings", "None")},
           {QStringLiteral("1"), QT_TRANSLATE_NOOP("SubtitleSettings", "Thin")},
           {QStringLiteral("2"), QT_TRANSLATE_NOOP("SubtitleSettings", "Narrow")},
           {QStringLiteral("4"), QT_TRANSLATE_NOOP("SubtitleSettings", "Wide")},
           {QStringLiteral("5"), QT_TRANSLATE_NOOP("SubtitleSettings", "Very wide")}},
          "subtitles.border_size"));
      section.settings.append(choiceSetting("borderColor", QString(),
                                            QT_TRANSLATE_NOOP("SubtitleSettings", "Outline colour"),
                                            colorChoices(QT_TRANSLATE_NOOP("SubtitleSettings", "Default (black)")),
                                            "subtitles.border_color"));
      section.settings.append(choiceSetting("backgroundColor", QString(),
                                            QT_TRANSLATE_NOOP("SubtitleSettings", "Background"),
                                            colorChoices(QT_TRANSLATE_NOOP("SubtitleSettings", "None")),
                                            "subtitles.background_color"));
      // Alpha as mpv takes it: FF is opaque.
      section.settings.append(choiceSetting(
          "backgroundAlpha", QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Background transparency"),
          {{QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Opaque")},
           {QStringLiteral("C0"), "25 %"},
           {QStringLiteral("80"), "50 %"},
           {QStringLiteral("40"), "75 %"}},
          "subtitles.background_transparency"));
      section.settings.append(choiceSetting(
          "placement", QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Position"),
          {{QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Bottom centre")},
           {QStringLiteral("left,bottom"), QT_TRANSLATE_NOOP("SubtitleSettings", "Bottom left")},
           {QStringLiteral("right,bottom"), QT_TRANSLATE_NOOP("SubtitleSettings", "Bottom right")},
           {QStringLiteral("center,top"), QT_TRANSLATE_NOOP("SubtitleSettings", "Top centre")},
           {QStringLiteral("left,top"), QT_TRANSLATE_NOOP("SubtitleSettings", "Top left")},
           {QStringLiteral("right,top"), QT_TRANSLATE_NOOP("SubtitleSettings", "Top right")}},
          "subtitles.placement"));

      // mpv's sub-ass-override; empty is mpv's default (scale).
      settings::SettingDescriptor assOverride = choiceSetting(
          "assOverride", QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Styled subtitles"),
          {{QString(), QT_TRANSLATE_NOOP("SubtitleSettings", "Their style, at the chosen size")},
           {QStringLiteral("no"), QT_TRANSLATE_NOOP("SubtitleSettings", "Their style as is")},
           {QStringLiteral("force"), QT_TRANSLATE_NOOP("SubtitleSettings", "The style above")},
           {QStringLiteral("strip"), QT_TRANSLATE_NOOP("SubtitleSettings", "Plain text")}},
          "subtitles.ass_style_override");
      assOverride.description = QT_TRANSLATE_NOOP(
          "SubtitleSettings", "How much of the style above applies to subtitles with their own styles (ASS).");
      section.settings.append(assOverride);
      settings::SettingDescriptor assScale =
          boolSetting("assScaleBorderAndShadow", true,
                      QT_TRANSLATE_NOOP("SubtitleSettings", "Scale outlines of styled subtitles with the video"),
                      "subtitles.ass_scale_border_and_shadow");
      section.settings.append(assScale);
      return registry.registerSection(std::move(section));
    }
  } // namespace

  bool registerPlayerSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("playback");
    section.context = "PlayerSettings";
    section.title = QT_TRANSLATE_NOOP("PlayerSettings", "Playback");
    section.order = 100;

    settings::SettingDescriptor autoSkip;
    autoSkip.key = QStringLiteral("autoSkipSegments");
    autoSkip.type = settings::SettingType::Bool;
    autoSkip.defaultValue = false;
    autoSkip.label = QT_TRANSLATE_NOOP("PlayerSettings", "Skip intros and credits automatically");
    autoSkip.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "When the server knows where they are. Otherwise a Skip button is shown.");
    section.settings.append(autoSkip);

    settings::SettingDescriptor autoNext;
    autoNext.key = QStringLiteral("autoNextEpisode");
    autoNext.type = settings::SettingType::Bool;
    autoNext.defaultValue = true;
    autoNext.label = QT_TRANSLATE_NOOP("PlayerSettings", "Play the next episode automatically");
    autoNext.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "When an episode ends. In a group the group's queue decides.");
    section.settings.append(autoNext);

    settings::SettingDescriptor volume;
    volume.key = QStringLiteral("defaultVolume");
    volume.type = settings::SettingType::Int;
    volume.defaultValue = 100;
    volume.minimum = 0;
    volume.maximum = 100;
    volume.step = 5;
    volume.unit = QStringLiteral("%");
    volume.label = QT_TRANSLATE_NOOP("PlayerSettings", "Volume at start");
    volume.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "The player's volume when the app starts. Changes in the player last until you quit.");
    section.settings.append(volume);

    // Values are mpv's hwdec modes.
    settings::SettingDescriptor hwdec;
    hwdec.key = QStringLiteral("hardwareDecoding");
    hwdec.type = settings::SettingType::Choice;
    hwdec.defaultValue = QStringLiteral("auto-safe");
    hwdec.label = QT_TRANSLATE_NOOP("PlayerSettings", "Hardware decoding");
    hwdec.description = QT_TRANSLATE_NOOP(
        "PlayerSettings",
        "Decodes video on the graphics card. Copy is slower but works with more drivers; turn it off if "
        "the picture is broken.");
    hwdec.choices = {{QStringLiteral("auto-safe"), QT_TRANSLATE_NOOP("PlayerSettings", "On")},
                     {QStringLiteral("auto-copy-safe"), QT_TRANSLATE_NOOP("PlayerSettings", "Copy")},
                     {QStringLiteral("no"), QT_TRANSLATE_NOOP("PlayerSettings", "Off")}};
    hwdec.legacyKey = QStringLiteral("video.hardwareDecoding");
    section.settings.append(hwdec);

    // ADR 0001: off by default, HDR files are tone-mapped to SDR. Shown
    // where the build can drive an HDR10 swap chain (Windows).
    settings::SettingDescriptor hdr;
    hdr.key = QStringLiteral("hdr10");
    hdr.type = settings::SettingType::Bool;
    hdr.defaultValue = false;
    hdr.label = QT_TRANSLATE_NOOP("PlayerSettings", "HDR on HDR displays");
    hdr.description = QT_TRANSLATE_NOOP(
        "PlayerSettings",
        "HDR10 videos play in HDR when Windows has HDR turned on for the display. Off: they are converted "
        "to SDR.");
    hdr.requiresRestart = true;
    hdr.hidden = !ui::HdrOutput::supported();
    section.settings.append(hdr);

    // Bits per second; "0" for no limit.
    settings::SettingDescriptor maxBitrate;
    maxBitrate.key = QStringLiteral("maxBitrate");
    maxBitrate.type = settings::SettingType::Choice;
    maxBitrate.defaultValue = QStringLiteral("0");
    maxBitrate.label = QT_TRANSLATE_NOOP("PlayerSettings", "Quality");
    maxBitrate.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "The most the server may stream; a file above it is transcoded down. The player menu changes it "
                          "for the session.");
    maxBitrate.choices = {{QStringLiteral("0"), QT_TRANSLATE_NOOP("PlayerSettings", "Original")},
                          {QStringLiteral("40000000"), QT_TRANSLATE_NOOP("PlayerSettings", "40 Mbit/s")},
                          {QStringLiteral("20000000"), QT_TRANSLATE_NOOP("PlayerSettings", "20 Mbit/s")},
                          {QStringLiteral("10000000"), QT_TRANSLATE_NOOP("PlayerSettings", "10 Mbit/s")},
                          {QStringLiteral("6000000"), QT_TRANSLATE_NOOP("PlayerSettings", "6 Mbit/s")},
                          {QStringLiteral("3000000"), QT_TRANSLATE_NOOP("PlayerSettings", "3 Mbit/s")},
                          {QStringLiteral("1500000"), QT_TRANSLATE_NOOP("PlayerSettings", "1.5 Mbit/s")}};
    section.settings.append(maxBitrate);

    for (const TranscodeSetting& transcode : TranscodeSettings) {
      settings::SettingDescriptor setting;
      setting.key = QLatin1String(transcode.key);
      setting.type = settings::SettingType::Bool;
      setting.defaultValue = false;
      setting.label = transcode.label;
      setting.description = transcode.description;
      setting.legacyKey = QLatin1String(transcode.legacyKey);
      section.settings.append(setting);
    }
    bool ok = registry.registerSection(std::move(section));
    ok = registerAudioSettings(registry) && ok;
    return registerSubtitleSettings(registry) && ok;
  }

  void requestHdrOutput(const settings::Settings& settings) {
    ui::HdrOutput::request(settings.value(HdrKey).toBool());
  }

  void installPlayer(AppServices& services, QQmlEngine&) {
    Q_ASSERT(services.session && services.client != nullptr && services.navigator != nullptr);
    services.playerModel =
        std::make_unique<ui::PlayerModel>(*services.session, *services.client, services.player.get());
    ui::PlayerModel& model = *services.playerModel;
    ui::PlayerSession::setInstance(&model);

    // The setting and the player's own switch stay in step.
    settings::Settings& settings = *services.settings;
    model.setAutoSkip(settings.value(AutoSkipKey).toBool());
    model.setAutoNext(settings.value(AutoNextKey).toBool());
    model.setVolume(settings.value(VolumeKey).toInt());
    media::MpvPlayer* player = services.player.get();
    if (player != nullptr) {
      player->setStringProperty("hwdec", settings.value(HwdecKey).toString().toUtf8());
      applyAudio(*player, settings);
      applySubtitles(*player, settings);
    }
    model.setTrackDefaults(trackDefaults(settings));
    model.setProfileOptions(profileOptions(settings));
    model.setMaxBitrate(settings.value(MaxBitrateKey).toString().toLongLong());
    QObject::connect(&settings, &settings::Settings::valueChanged, &model,
                     [&model, &settings, player](const QString& key, const QVariant& value) {
                       if (key == AutoSkipKey) model.setAutoSkip(value.toBool());
                       else if (isTranscodeKey(key))
                         model.setProfileOptions(profileOptions(settings));
                       else if (key == MaxBitrateKey)
                         model.setMaxBitrate(value.toString().toLongLong());
                       else if (key == AudioLanguageKey || key == SubtitleLanguageKey || key == SubtitleModeKey)
                         model.setTrackDefaults(trackDefaults(settings));
                       else if (key == AutoNextKey) model.setAutoNext(value.toBool());
                       else if (player == nullptr) return;
                       else if (key == HwdecKey) player->setStringProperty("hwdec", value.toString().toUtf8());
                       else if (key.startsWith(AudioSection)) applyAudio(*player, settings);
                       else if (key.startsWith(SubtitleSection)) applySubtitles(*player, settings);
                     });
    QObject::connect(&model, &ui::PlayerModel::autoSkipChanged, &settings,
                     [&model, &settings] { settings.setValue(AutoSkipKey, model.autoSkip()); });

    ui::Navigator& navigator = *services.navigator;
    navigator.registerPage(QStringLiteral("player"), QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Player/PlayerPage.qml")));
    if (services.libraryUi != nullptr) {
      services.libraryUi->setPlayHandler([&model, &navigator](const QString& itemId, qint64 startTicks) {
        model.play(itemId, startTicks);
        navigator.navigate(QStringLiteral("player"));
      });
    }
  }

} // namespace jfd::desktop
