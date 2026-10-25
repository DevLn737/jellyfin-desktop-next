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

    // mpv's own subtitle font size, used for "Default".
    constexpr int MpvSubtitleFontSize = 38;

    // Settings that go straight to an mpv property, at start and on change.
    void applyToMpv(media::MpvPlayer& player, const QString& key, const QVariant& value) {
      if (key == HwdecKey) {
        player.setStringProperty("hwdec", value.toString().toUtf8());
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
      for (const QString& key : {HwdecKey, SubtitleSizeKey}) applyToMpv(*player, key, settings.value(key));
    QObject::connect(&settings, &settings::Settings::valueChanged, &model,
                     [&model, player](const QString& key, const QVariant& value) {
                       if (key == AutoSkipKey) model.setAutoSkip(value.toBool());
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
