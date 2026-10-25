#pragma once

#include <jfd/integrations/AppImageUpdater.h>
#include <jfd/integrations/DiscordPresence.h>
#include <jfd/integrations/MediaSessionBridge.h>
#include <jfd/integrations/UpdateChecker.h>
#include <jfd/platform/MediaControls.h>
#include <jfd/platform/SleepInhibitor.h>
#include <jfd/session/SessionHub.h>

#include <QNetworkAccessManager>
#include <QString>

#include <memory>

class QQmlEngine;
class QQuickWindow;

namespace jfd::api {
  class JellyfinClient;
}

namespace jfd::desktop {

  struct AppServices;

  // The system media controls (SMTC, MPRIS), sleep inhibition while video
  // plays, Discord Rich Presence, the update check and the AppImage update,
  // fed from the session.
  struct IntegrationServices {
    QNetworkAccessManager network;
    std::unique_ptr<platform::MediaControls> mediaControls;
    std::unique_ptr<platform::SleepInhibitor> sleepInhibitor;
    integrations::DiscordPresence presence;
    std::unique_ptr<integrations::UpdateChecker> updates;
    std::unique_ptr<integrations::AppImageUpdater> appImage;
    // Something plays or this client is in a group: no update restarts it.
    bool busy = false;
    // A downloaded update waits for playback to end before it is offered.
    bool offerWhenIdle = false;
    // Set by connectIntegrations; the controls' commands go nowhere before.
    integrations::MediaSessionBridge::Post post;
    std::unique_ptr<integrations::MediaSessionBridge> bridge;
    // The item whose details were asked for last.
    QString detailsItem;
    // Last: released first, before what its handler uses.
    session::SessionHub::Subscription subscription;
  };

  // Creates the services and follows the Discord setting.
  void installIntegrations(AppServices& services, QQmlEngine& engine);

  // Feeds the session's snapshots to the bridge and its commands back to
  // post; asks the server for each new item's details (series, poster).
  void connectIntegrations(AppServices& services, session::SessionHub& hub,
                           integrations::MediaSessionBridge::Post post, api::JellyfinClient& client);

  // After the window loads: media controls belong to it (SMTC needs its
  // handle), and a newer release is shown in its toast. checkForUpdates is
  // false in the smoke test.
  void startIntegrations(AppServices& services, QQuickWindow& window, bool checkForUpdates);

} // namespace jfd::desktop
