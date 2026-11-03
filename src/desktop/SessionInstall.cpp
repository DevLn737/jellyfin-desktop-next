#include "AppServices.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/library/SignIn.h>
#include <jfd/music/MusicPlayer.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/PlayerModel.h>

#include <MpvMediaBackend.h>
#include <MpvPlayer.h>

#include <QLoggingCategory>
#include <QTimer>

#include <memory>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcSession, "jfd.app.session")

    // Quitting stays under the one-second shutdown budget even when the
    // server is slow to answer the group leave or the playback reports.
    constexpr int LeaveTimeoutMs = 700;

    api::SessionCapabilities capabilities() {
      api::SessionCapabilities capabilities;
      // The mpv backend applies group playback rates.
      capabilities.supportsSyncPlayPlaybackRate = true;
      capabilities.deviceProfile = api::buildDeviceProfile({});
      return capabilities;
    }

    // The client keeps the value, and the session socket reports it again
    // whenever it connects, so the server session always has it.
    void reportCapabilities(api::JellyfinClient& client) {
      client.reportCapabilities(capabilities(), &client, [](auto reported) {
        if (!reported) {
          qCWarning(lcSession) << "capabilities not accepted:" << reported.error().message;
        }
      });
    }
  } // namespace

  void installSession(AppServices& services) {
    Q_ASSERT(services.client != nullptr && services.socket != nullptr && services.signIn != nullptr);
    api::JellyfinClient& client = *services.client;

    services.player = std::make_shared<media::MpvPlayer>(media::MpvPlayer::Options{});
    if (!services.player->isValid()) {
      qCWarning(lcSession) << "mpv did not start: playback is unavailable";
    }
    media::MpvPlayer& player = *services.player;
    const auto backend = [&player](const core::Clock& clock, session::SessionRuntime::Post post) {
      return std::make_unique<media::MpvMediaBackend>(player, clock, std::move(post));
    };
    session::SessionRuntime::Settings settings;
    settings.deviceProfile = capabilities().deviceProfile;
    services.session =
        std::make_shared<session::SessionRuntime>(client, *services.socket, backend, std::move(settings));

    library::SignIn& signIn = *services.signIn;
    QObject::connect(&signIn, &library::SignIn::signedIn, &client, [&client] { reportCapabilities(client); });
    // installLibrary restores the saved session before this runs.
    if (signIn.step() == library::SignIn::Step::SignedIn) {
      reportCapabilities(client);
    }
  }

  void prepareExit(AppServices& services, std::function<void()> done) {
    // The playing file's Stopped report (so the server records the exit
    // position and marks a finished item played) and the group leave go out
    // together; done runs once all are answered or the budget runs out.
    struct Pending {
      int left = 1;
      std::function<void()> done;
    };
    auto pending = std::make_shared<Pending>();
    pending->done = std::move(done);
    const auto finishOne = [pending] {
      if (!pending->done || --pending->left > 0) return;
      auto callback = std::move(pending->done);
      pending->done = nullptr;
      callback();
    };
    const auto finishAll = [pending] {
      if (!pending->done) return;
      auto callback = std::move(pending->done);
      pending->done = nullptr;
      callback();
    };
    if (services.playerModel) {
      ++pending->left;
      services.playerModel->endReport(finishOne);
    }
    if (services.music) {
      ++pending->left;
      services.music->endReport(finishOne);
    }
    if (services.session) {
      ++pending->left;
      services.session->leaveBeforeExit(finishOne, LeaveTimeoutMs);
    }
    if (pending->left > 1) QTimer::singleShot(LeaveTimeoutMs, finishAll);
    finishOne();
  }

} // namespace jfd::desktop
