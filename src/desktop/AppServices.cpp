#include "AppServices.h"

#include <jfd/settings/CommonSections.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/SettingsModel.h>

#include <QDesktopServices>
#include <QDir>
#include <QQmlEngine>
#include <QUrl>

namespace jfd::desktop {

  void installShell(AppServices& services, QQmlEngine& engine) {
    services.navigator = engine.singletonInstance<ui::Navigator*>("Jfd.Ui.Shell", "Navigator");
    services.appSettings = engine.singletonInstance<ui::AppSettings*>("Jfd.Ui.Settings", "AppSettings");
    Q_ASSERT(services.navigator != nullptr && services.appSettings != nullptr);

    services.appSettings->attach(services.settings.get());

    ui::Navigator& navigator = *services.navigator;
    navigator.setPlaceholderPage(QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Shell/PlaceholderPage.qml")));
    navigator.registerPage(QStringLiteral("settings"),
                           QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Settings/SettingsPage.qml")));

    QObject::connect(services.settings.get(), &settings::Settings::actionTriggered, &navigator,
                     [&services](const QString& key) {
                       if (key == settings::keys::OpenLogFolder) {
                         QDir().mkpath(services.logDirectory);
                         QDesktopServices::openUrl(QUrl::fromLocalFile(services.logDirectory));
                       }
                       // Sign out and Change server: installLibrary.
                     });
  }

} // namespace jfd::desktop
