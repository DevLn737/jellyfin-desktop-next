#pragma once

#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>

#include <QString>

#include <memory>

class QQmlEngine;

namespace jfd::ui {
  class AppSettings;
  class Navigator;
} // namespace jfd::ui

namespace jfd::desktop {

  struct IntegrationServices;

  // The composition root: every long-lived service of the application,
  // created in startup order and destroyed in reverse.
  //
  // Each module that takes part gives one function, called from main.cpp
  // in the marked places:
  //   bool register<Module>Settings(settings::SettingsRegistry&);   // before settings open
  //   void install<Module>(AppServices&, QQmlEngine&);              // before the window loads
  // An install function creates the module's services, keeps them in its
  // own member here, registers its QML pages with the navigator and its
  // singletons with the engine.
  struct AppServices {
    settings::SettingsRegistry settingsRegistry;
    std::unique_ptr<settings::Settings> settings;

    // QML singletons, owned by the engine.
    ui::Navigator* navigator = nullptr;
    ui::AppSettings* appSettings = nullptr;

    QString logDirectory;

    // installIntegrations; complete in main.cpp, which destroys it.
    std::unique_ptr<IntegrationServices> integrations;
  };

  // The shell and settings screen: connects AppSettings to the store,
  // registers the settings page and the placeholder, and handles the
  // diagnostics actions.
  void installShell(AppServices& services, QQmlEngine& engine);

} // namespace jfd::desktop
