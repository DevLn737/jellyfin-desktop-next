#pragma once

#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>

#include <QString>

#include <functional>
#include <memory>

class QQmlEngine;
class QQuickWindow;

namespace jfd::api {
  class JellyfinClient;
  class SessionSocket;
} // namespace jfd::api

namespace jfd::library {
  class SignIn;
}

namespace jfd::media {
  class MpvPlayer;
}

namespace jfd::session {
  class SessionRuntime;
}

namespace jfd::ui {
  class AppSettings;
  class LibraryUi;
  class Navigator;
  class PlayerModel;
} // namespace jfd::ui

namespace jfd::desktop {

  struct IntegrationServices;
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
    library::SignIn* signIn = nullptr;
    // Play and Watch together from the library screens go to the handlers
    // set on it (setPlayHandler, setWatchTogetherHandler).
    ui::LibraryUi* libraryUi = nullptr;

    // Playback (installSession): one mpv player and the session on it,
    // on the library's client and socket. The group and player modules
    // use these.
    std::shared_ptr<media::MpvPlayer> player;
    std::shared_ptr<session::SessionRuntime> session;
    // The player screen's model (installPlayer), on the session above.
    std::unique_ptr<ui::PlayerModel> playerModel;

    // installIntegrations; complete in main.cpp, which destroys it.
    std::unique_ptr<IntegrationServices> integrations;
  };

  // The shell and settings screen: connects AppSettings to the store,
  // registers the settings page and the placeholder, and handles the
  // diagnostics actions.
  void installShell(AppServices& services, QQmlEngine& engine);

  // After the window loads: sets Theme.reducedEffects from the "Simplified
  // effects" setting; on automatic, from the GPU the window renders on.
  void startEffects(AppServices& services, QQmlEngine& engine, QQuickWindow& window);

  // The device id the server knows this installation by.
  bool registerLibrarySettings(settings::SettingsRegistry& registry);
  // Sign-in, home, library, item and search pages, the image provider, and
  // the server connection. Signs in with the saved session when there is
  // one; otherwise the navigator is left empty for the login page.
  void installLibrary(AppServices& services, QQmlEngine& engine);

  // The mpv player and the playback session. Reports this client's
  // capabilities (SyncPlay playback rates, the device profile) whenever
  // a sign-in completes, so the session socket registers with them.
  // Needs installLibrary.
  void installSession(AppServices& services);

  // The playback settings section (auto-skip of intros and credits).
  bool registerPlayerSettings(settings::SettingsRegistry& registry);
  // The player page ("player" route) and the PlayerSession singleton; Play
  // on the library screens opens it. Needs installSession.
  void installPlayer(AppServices& services, QQmlEngine& engine);

  // Leaves a SyncPlay group before quitting; calls done when the server
  // answered or after a short timeout, so quitting never hangs on the
  // network.
  void prepareExit(AppServices& services, std::function<void()> done);

} // namespace jfd::desktop
