#include "AppServices.h"

#include <jfd/ui/CommandRegistry.h>
#include <jfd/ui/Navigator.h>

#include <QCoreApplication>
#include <QQmlEngine>
#include <QSet>
#include <QVariantMap>

#include <memory>

namespace jfd::desktop {

  namespace {
    constexpr QLatin1String NavigatePrefix{"navigate."};
  } // namespace

  void installCommands(AppServices& services, QQmlEngine& engine) {
    auto* commands = engine.singletonInstance<ui::CommandRegistry*>("Jfd.Ui.Shell", "CommandRegistry");
    ui::Navigator* navigator = services.navigator;
    Q_ASSERT(commands != nullptr && navigator != nullptr);

    ui::CommandRegistry::Command back;
    back.id = QStringLiteral("navigate.back");
    back.title = QCoreApplication::translate("Commands", "Go back");
    back.available = [navigator] { return navigator->canGoBack(); };
    back.run = [navigator] { navigator->back(); };
    commands->add(std::move(back));

    // One "Open <name>" for each destination in the sidebar, kept in step
    // with it as modules register theirs.
    auto known = std::make_shared<QSet<QString>>();
    const auto sync = [commands, navigator, known] {
      QSet<QString> now;
      for (const QVariant& entry : navigator->sidebarEntries()) {
        const QVariantMap map = entry.toMap();
        const QString route = map.value(QStringLiteral("route")).toString();
        const QString id = NavigatePrefix + route;
        now.insert(id);
        ui::CommandRegistry::Command open;
        open.id = id;
        open.title = QCoreApplication::translate("Commands", "Open %1").arg(map.value(QStringLiteral("text")).toString());
        open.run = [navigator, route] { navigator->navigate(route); };
        commands->add(std::move(open));
      }
      for (const QString& id : std::as_const(*known)) {
        if (!now.contains(id)) {
          commands->remove(id);
        }
      }
      *known = now;
    };
    QObject::connect(navigator, &ui::Navigator::sidebarChanged, commands, sync);
    sync();
  }

} // namespace jfd::desktop
