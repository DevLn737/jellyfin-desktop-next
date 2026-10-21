#include "AppServices.h"

#include <jfd/session/SessionRuntime.h>
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
    return registry.registerSection(std::move(section));
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
