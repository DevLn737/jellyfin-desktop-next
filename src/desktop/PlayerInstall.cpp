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
    QObject::connect(&settings, &settings::Settings::valueChanged, &model,
                     [&model](const QString& key, const QVariant& value) {
                       if (key == AutoSkipKey) model.setAutoSkip(value.toBool());
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
