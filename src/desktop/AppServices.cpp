#include "AppServices.h"

#include <jfd/settings/CommonSections.h>
#include <jfd/ui/GraphicsInfo.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/SettingsModel.h>

#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcEffects, "jfd.app.effects")
  } // namespace

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

  void startEffects(AppServices& services, QQmlEngine& engine, QQuickWindow& window) {
    QObject* theme = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme");
    Q_ASSERT(theme != nullptr);
    // Until the GPU is known, automatic means full effects.
    auto detected = std::make_shared<bool>(false);
    const auto apply = [&services, theme, detected] {
      const QString mode = services.settings->value(settings::keys::Effects).toString();
      const bool reduced = mode == QLatin1String("on") || (mode == QLatin1String("auto") && *detected);
      theme->setProperty("reducedEffects", reduced);
    };
    apply();
    QObject::connect(services.settings.get(), &settings::Settings::valueChanged, theme,
                     [apply](const QString& key) {
                       if (key == settings::keys::Effects) {
                         apply();
                       }
                     });
    ui::probeGraphics(window, [detected, apply](const ui::GraphicsDevice& device) {
      *detected = ui::prefersReducedEffects(device);
      static const char* const kinds[] = {"unknown", "integrated", "discrete", "software"};
      qCInfo(lcEffects).nospace().noquote()
          << "graphics: " << device.api << ", \"" << device.name << "\", vendor 0x" << Qt::hex
          << device.vendorId << Qt::dec << ", " << kinds[int(device.kind)]
          << "; simplified effects on automatic: " << (*detected ? "on" : "off");
      apply();
    });
  }

} // namespace jfd::desktop
