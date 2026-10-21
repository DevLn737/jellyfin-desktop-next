#include "AppServices.h"

#include <jfd/library/SignIn.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>
#include <jfd/ui/GroupSession.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QQmlEngine>
#include <QUrl>

namespace jfd::desktop {

  namespace {
    const QString SpeedCorrectionKey = QStringLiteral("syncplay/speedCorrection");
    const QString SeekThresholdKey = QStringLiteral("syncplay/seekThresholdMs");

    // The reconciler's defaults (Jellyfin Web's), with the user's two choices.
    core::ReconcilerSettings syncCorrection(const settings::Settings& settings) {
      core::ReconcilerSettings reconciler;
      reconciler.useSpeed = settings.value(SpeedCorrectionKey).toBool();
      reconciler.skipMinimum = core::MediaDelta::fromMilliseconds(settings.value(SeekThresholdKey).toInt());
      return reconciler;
    }
  } // namespace

  bool registerGroupSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("syncplay");
    section.context = "GroupSettings";
    section.title = QT_TRANSLATE_NOOP("GroupSettings", "Watch together");
    section.order = 110;

    settings::SettingDescriptor speed;
    speed.key = QStringLiteral("speedCorrection");
    speed.type = settings::SettingType::Bool;
    speed.defaultValue = true;
    speed.label = QT_TRANSLATE_NOOP("GroupSettings", "Catch up by changing speed");
    speed.description = QT_TRANSLATE_NOOP(
        "GroupSettings",
        "Small differences from the group are made up by playing a little faster or slower for a moment, "
        "without a jump. Off: only by seeking.");
    section.settings.append(speed);

    settings::SettingDescriptor threshold;
    threshold.key = QStringLiteral("seekThresholdMs");
    threshold.type = settings::SettingType::Int;
    threshold.defaultValue = 400;
    threshold.minimum = 100;
    threshold.maximum = 3000;
    threshold.step = 50;
    threshold.unit = QStringLiteral("ms");
    threshold.label = QT_TRANSLATE_NOOP("GroupSettings", "Seek when out of step by more than");
    threshold.description = QT_TRANSLATE_NOOP(
        "GroupSettings",
        "With speed correction on, differences up to 3 seconds are made up by speed first.");
    section.settings.append(threshold);
    return registry.registerSection(std::move(section));
  }

  void installGroup(AppServices& services, QQmlEngine&) {
    Q_ASSERT(services.session && services.client != nullptr && services.socket != nullptr);
    services.group = std::make_unique<session::GroupModel>(*services.session, *services.client, *services.socket);
    session::GroupModel& group = *services.group;
    ui::GroupSession::setInstance(&group);

    // The correction settings apply while playing, from the next decision.
    session::SessionRuntime& runtime = *services.session;
    settings::Settings& settings = *services.settings;
    runtime.setSyncCorrection(syncCorrection(settings));
    QObject::connect(&settings, &settings::Settings::valueChanged, &group,
                     [&runtime, &settings](const QString& key, const QVariant&) {
                       if (key == SpeedCorrectionKey || key == SeekThresholdKey)
                         runtime.setSyncCorrection(syncCorrection(settings));
                     });
    services.navigator->registerPage(QStringLiteral("group"),
                                     QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Group/GroupPage.qml")));

    // Another server may or may not have the playback-rate extension.
    if (services.signIn != nullptr) {
      QObject::connect(services.signIn, &library::SignIn::signedIn, &group, &session::GroupModel::checkRateSupport);
    }

    // "Watch together" on an item page: a group named after the item that
    // starts it for everyone (or the item played in the group we are in),
    // then the group page with its members and queue.
    if (services.libraryUi != nullptr) {
      ui::Navigator* navigator = services.navigator;
      services.libraryUi->setWatchTogetherHandler(
          [&group, navigator](const QString& itemId, const QString& name, qint64 startPositionTicks) {
            group.watchTogether(itemId, name, startPositionTicks);
            navigator->navigate(QStringLiteral("group"));
          });
    }
  }

  void uninstallGroup(AppServices& services) {
    // The watch-together handler stays on LibraryUi: nothing calls it once
    // the window is gone, and LibraryUi's signals must not fire after the
    // QML engine that bound them.
    ui::GroupSession::setInstance(nullptr);
    services.group.reset();
  }

} // namespace jfd::desktop
