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

namespace jfd::platform {
  class CredentialStore;
}

namespace jfd::seerr {
  class SeerrService;
}

namespace jfd::session {
  class GroupModel;
  class SessionRuntime;
} // namespace jfd::session

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
    // The graphics API and GPU Qt renders on; set by startEffects when the
    // scene graph is up, for the diagnostic report.
    QString graphicsDescription;
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
    // Watching together (installGroup): the group page's model, on the
    // session. Declared after it so it goes first.
    std::unique_ptr<session::GroupModel> group;

    // installSeerr: the Seerr connection and the credential storage its
    // session cookie is kept in (declared first so it goes last).
    std::unique_ptr<platform::CredentialStore> seerrCredentials;
    std::unique_ptr<seerr::SeerrService> seerr;

    // installIntegrations; complete in main.cpp, which destroys it.
    std::unique_ptr<IntegrationServices> integrations;
  };

  // The shell and settings screen: connects AppSettings to the store,
  // registers the settings page and the placeholder, and handles the
  // diagnostics actions.
  void installShell(AppServices& services, QQmlEngine& engine);

  // The Ctrl+K commands of the shell itself: Go back and one "Open <name>"
  // for each sidebar destination (kept in step with the sidebar). Needs
  // installShell.
  void installCommands(AppServices& services, QQmlEngine& engine);

  // After the window loads: starts the frame and process statistics and
  // saves a diagnostic report when Settings > Save diagnostic report is
  // pressed (reports go to <logDirectory>/reports, the last five are kept).
  void installDiagnostics(AppServices& services, QQuickWindow& window);

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

  // The playback settings section (auto-skip of intros and credits, HDR10
  // output).
  bool registerPlayerSettings(settings::SettingsRegistry& registry);
  // Before QGuiApplication: asks for an HDR10 swap chain when the setting is
  // on (Windows).
  void requestHdrOutput(const settings::Settings& settings);
  // The player page ("player" route) and the PlayerSession singleton; Play
  // on the library screens opens it. Needs installSession.
  void installPlayer(AppServices& services, QQmlEngine& engine);
  // The watch-together settings section (speed correction, seek threshold).
  bool registerGroupSettings(settings::SettingsRegistry& registry);
  // The watch-together page (route "group"), the GroupSession singleton
  // for it and the player's group indicator, and "Watch together" on item
  // pages. Needs installSession. uninstallGroup, after the QML engine is
  // gone, drops the model before the session it reads.
  void installGroup(AppServices& services, QQmlEngine& engine);
  void uninstallGroup(AppServices& services);

  // The Seerr settings section (address, sign in, check, sign out).
  bool registerSeerrSettings(settings::SettingsRegistry& registry);
  // Requests: the Seerr connection, its routes ("requests" in the sidebar
  // only while an address is set) and the toasts of the connection check.
  // Needs installShell. uninstallSeerr, after the QML engine is gone, drops
  // the service.
  void installSeerr(AppServices& services, QQmlEngine& engine);
  void uninstallSeerr(AppServices& services);

  // Leaves a SyncPlay group before quitting; calls done when the server
  // answered or after a short timeout, so quitting never hangs on the
  // network.
  void prepareExit(AppServices& services, std::function<void()> done);

} // namespace jfd::desktop
