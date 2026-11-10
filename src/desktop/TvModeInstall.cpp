#include "AppServices.h"

#include <jfd/settings/CommonSections.h>
#include <jfd/settings/Settings.h>
#include <jfd/ui/CommandRegistry.h>

#include <QCoreApplication>
#include <QQmlEngine>

#include <memory>

namespace jfd::desktop {

  void installTvMode(AppServices& services, QQmlEngine& engine, bool forcedOn) {
    QObject* theme = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme");
    auto* commands = engine.singletonInstance<ui::CommandRegistry*>("Jfd.Ui.Shell", "CommandRegistry");
    Q_ASSERT(theme != nullptr && commands != nullptr);
    settings::Settings& settings = *services.settings;

    // --tv: on for this run, until the setting or the command changes it.
    auto forced = std::make_shared<bool>(forcedOn);
    const auto apply = [theme, &settings, forced] {
      theme->setProperty("tvMode", *forced || settings.value(settings::keys::TvMode).toBool());
      theme->setProperty("tvSafeArea", settings.value(settings::keys::TvSafeArea).toBool());
    };
    apply();
    QObject::connect(&settings, &settings::Settings::valueChanged, theme, [apply, forced](const QString& key) {
      if (key == settings::keys::TvMode) *forced = false;
      if (key == settings::keys::TvMode || key == settings::keys::TvSafeArea) apply();
    });

    ui::CommandRegistry::Command toggle;
    toggle.id = QStringLiteral("interface.tvMode");
    toggle.title = QCoreApplication::translate("Commands", "TV mode");
    toggle.run = [theme, &settings, forced, apply] {
      const bool on = theme->property("tvMode").toBool();
      *forced = false;
      settings.setValue(settings::keys::TvMode, !on);
      apply();
    };
    commands->add(std::move(toggle));
  }

} // namespace jfd::desktop
