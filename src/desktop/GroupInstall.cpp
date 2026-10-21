#include "AppServices.h"

#include <jfd/library/SignIn.h>
#include <jfd/session/GroupModel.h>
#include <jfd/ui/GroupSession.h>
#include <jfd/ui/LibraryUi.h>
#include <jfd/ui/Navigator.h>

#include <QQmlEngine>
#include <QUrl>

namespace jfd::desktop {

  void installGroup(AppServices& services, QQmlEngine&) {
    Q_ASSERT(services.session && services.client != nullptr && services.socket != nullptr);
    services.group = std::make_unique<session::GroupModel>(*services.session, *services.client, *services.socket);
    session::GroupModel& group = *services.group;
    ui::GroupSession::setInstance(&group);
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
