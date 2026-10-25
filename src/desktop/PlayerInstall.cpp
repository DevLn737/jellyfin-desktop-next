#include "AppServices.h"

#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/HdrOutput.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/PlayerModel.h>
#include <jfd/ui/PlayerSession.h>

#include <MpvPlayer.h>

#include <QCoreApplication>
#include <QUrl>

namespace jfd::desktop {

  namespace {
    const QString AutoSkipKey = QStringLiteral("playback/autoSkipSegments");
    const QString HdrKey = QStringLiteral("playback/hdr10");
    const QString VolumeKey = QStringLiteral("playback/defaultVolume");
    const QString AutoNextKey = QStringLiteral("playback/autoNextEpisode");
    const QString HwdecKey = QStringLiteral("playback/hardwareDecoding");
    const QString SubtitleSizeKey = QStringLiteral("playback/subtitleSize");
    const QString SubtitleStyleKey = QStringLiteral("playback/subtitleStyleOverride");
    const QString AudioLanguageKey = QStringLiteral("playback/audioLanguage");
    const QString SubtitleLanguageKey = QStringLiteral("playback/subtitleLanguage");
    const QString SubtitleModeKey = QStringLiteral("playback/subtitleMode");

    // ISO 639-2/B codes, as Jellyfin names stream languages.
    QList<settings::Choice> languageChoices() {
      return {{QString(), QT_TRANSLATE_NOOP("PlayerSettings", "As on the server")},
              {QStringLiteral("rus"), QT_TRANSLATE_NOOP("PlayerSettings", "Russian")},
              {QStringLiteral("eng"), QT_TRANSLATE_NOOP("PlayerSettings", "English")},
              {QStringLiteral("jpn"), QT_TRANSLATE_NOOP("PlayerSettings", "Japanese")},
              {QStringLiteral("ukr"), QT_TRANSLATE_NOOP("PlayerSettings", "Ukrainian")},
              {QStringLiteral("ger"), QT_TRANSLATE_NOOP("PlayerSettings", "German")},
              {QStringLiteral("fre"), QT_TRANSLATE_NOOP("PlayerSettings", "French")},
              {QStringLiteral("spa"), QT_TRANSLATE_NOOP("PlayerSettings", "Spanish")},
              {QStringLiteral("ita"), QT_TRANSLATE_NOOP("PlayerSettings", "Italian")},
              {QStringLiteral("kor"), QT_TRANSLATE_NOOP("PlayerSettings", "Korean")},
              {QStringLiteral("chi"), QT_TRANSLATE_NOOP("PlayerSettings", "Chinese")}};
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

    // mpv's own subtitle font size, used for "Default".
    constexpr int MpvSubtitleFontSize = 38;

    // Settings that go straight to an mpv property, at start and on change.
    void applyToMpv(media::MpvPlayer& player, const QString& key, const QVariant& value) {
      if (key == HwdecKey) {
        player.setStringProperty("hwdec", value.toString().toUtf8());
      } else if (key == SubtitleStyleKey) {
        const QByteArray mode = value.toString().toUtf8();
        player.setStringProperty("sub-ass-override", mode.isEmpty() ? QByteArrayLiteral("scale") : mode);
      } else if (key == SubtitleSizeKey) {
        const int size = value.toString().toInt();
        player.setDoubleProperty("sub-font-size", size > 0 ? size : MpvSubtitleFontSize);
      }
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
    volume.unit = "%";
    volume.label = QT_TRANSLATE_NOOP("PlayerSettings", "Volume at start");
    volume.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "The player's volume when the app starts. Changes in the player last until you quit.");
    section.settings.append(volume);

    settings::SettingDescriptor audioLanguage;
    audioLanguage.key = QStringLiteral("audioLanguage");
    audioLanguage.type = settings::SettingType::Choice;
    audioLanguage.defaultValue = QString();
    audioLanguage.label = QT_TRANSLATE_NOOP("PlayerSettings", "Audio language");
    audioLanguage.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "Until you pick a track yourself; your pick then carries to the next episodes.");
    audioLanguage.choices = languageChoices();
    section.settings.append(audioLanguage);

    settings::SettingDescriptor subtitleMode;
    subtitleMode.key = QStringLiteral("subtitleMode");
    subtitleMode.type = settings::SettingType::Choice;
    subtitleMode.defaultValue = QStringLiteral("default");
    subtitleMode.label = QT_TRANSLATE_NOOP("PlayerSettings", "Subtitles");
    subtitleMode.choices = {
        {QStringLiteral("default"), QT_TRANSLATE_NOOP("PlayerSettings", "As on the server")},
        {QStringLiteral("always"), QT_TRANSLATE_NOOP("PlayerSettings", "Always, in the subtitle language")},
        {QStringLiteral("forced"), QT_TRANSLATE_NOOP("PlayerSettings", "Only forced (signs and songs)")},
        {QStringLiteral("off"), QT_TRANSLATE_NOOP("PlayerSettings", "Off")}};
    section.settings.append(subtitleMode);

    settings::SettingDescriptor subtitleLanguage;
    subtitleLanguage.key = QStringLiteral("subtitleLanguage");
    subtitleLanguage.type = settings::SettingType::Choice;
    subtitleLanguage.defaultValue = QString();
    subtitleLanguage.label = QT_TRANSLATE_NOOP("PlayerSettings", "Subtitle language");
    subtitleLanguage.choices = languageChoices();
    section.settings.append(subtitleLanguage);

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

    // The 2.x client's sizes (mpv sub-font-size), so its value imports as is.
    settings::SettingDescriptor subtitleSize;
    subtitleSize.key = QStringLiteral("subtitleSize");
    subtitleSize.type = settings::SettingType::Choice;
    subtitleSize.defaultValue = QStringLiteral("-1");
    subtitleSize.label = QT_TRANSLATE_NOOP("PlayerSettings", "Subtitle size");
    subtitleSize.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "Text subtitles without their own styles. Styled ones keep their size.");
    subtitleSize.choices = {{QStringLiteral("-1"), QT_TRANSLATE_NOOP("PlayerSettings", "Default")},
                            {QStringLiteral("18"), QT_TRANSLATE_NOOP("PlayerSettings", "Tiny")},
                            {QStringLiteral("24"), QT_TRANSLATE_NOOP("PlayerSettings", "Small")},
                            {QStringLiteral("32"), QT_TRANSLATE_NOOP("PlayerSettings", "Normal")},
                            {QStringLiteral("42"), QT_TRANSLATE_NOOP("PlayerSettings", "Large")},
                            {QStringLiteral("60"), QT_TRANSLATE_NOOP("PlayerSettings", "Huge")}};
    subtitleSize.legacyKey = QStringLiteral("subtitles.size");
    section.settings.append(subtitleSize);

    // mpv's sub-ass-override; empty is mpv's default (scale).
    settings::SettingDescriptor subtitleStyle;
    subtitleStyle.key = QStringLiteral("subtitleStyleOverride");
    subtitleStyle.type = settings::SettingType::Choice;
    subtitleStyle.defaultValue = QString();
    subtitleStyle.label = QT_TRANSLATE_NOOP("PlayerSettings", "Styled subtitles");
    subtitleStyle.description = QT_TRANSLATE_NOOP(
        "PlayerSettings", "How much of the player's subtitle settings applies to subtitles with their own styles (ASS).");
    subtitleStyle.choices = {{QString(), QT_TRANSLATE_NOOP("PlayerSettings", "Their style, at the chosen scale")},
                             {QStringLiteral("no"), QT_TRANSLATE_NOOP("PlayerSettings", "Their style as is")},
                             {QStringLiteral("force"), QT_TRANSLATE_NOOP("PlayerSettings", "The player's style")},
                             {QStringLiteral("strip"), QT_TRANSLATE_NOOP("PlayerSettings", "Plain text")}};
    subtitleStyle.legacyKey = QStringLiteral("subtitles.ass_style_override");
    section.settings.append(subtitleStyle);

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
    return registry.registerSection(std::move(section));
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
    if (player != nullptr)
      for (const QString& key : {HwdecKey, SubtitleSizeKey, SubtitleStyleKey})
        applyToMpv(*player, key, settings.value(key));
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
                       else if (player != nullptr) applyToMpv(*player, key, value);
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
