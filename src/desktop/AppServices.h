#pragma once

#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>

#include <QString>

#include <memory>

class QQmlEngine;

namespace jfd::api {
  class JellyfinClient;
  class SessionSocket;
} // namespace jfd::api

namespace jfd::ui {
  class AppSettings;
  class LibraryUi;
  class Navigator;
} // namespace jfd::ui

namespace jfd::desktop {

  struct LibraryServices;

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
    // Downloaded images; empty keeps them in memory only.
    QString cacheDirectory;
    // The smoke test's temporary profile: nothing goes to the system
    // credential store.
    bool isolatedProfile = false;

    // The server connection and the library's models (installLibrary). The
    // session and group modules use the same client and socket.
    std::shared_ptr<LibraryServices> library;
    api::JellyfinClient* client = nullptr;
    api::SessionSocket* socket = nullptr;
    // Play and Watch together from the library screens go to the handlers
    // set on it (setPlayHandler, setWatchTogetherHandler).
    ui::LibraryUi* libraryUi = nullptr;
  };

  // The shell and settings screen: connects AppSettings to the store,
  // registers the settings page and the placeholder, and handles the
  // diagnostics actions.
  void installShell(AppServices& services, QQmlEngine& engine);

  // The device id the server knows this installation by.
  bool registerLibrarySettings(settings::SettingsRegistry& registry);
  // Sign-in, home, library, item and search pages, the image provider, and
  // the server connection. Signs in with the saved session when there is
  // one; otherwise the navigator is left empty for the login page.
  void installLibrary(AppServices& services, QQmlEngine& engine);

} // namespace jfd::desktop
