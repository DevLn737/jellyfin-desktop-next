#include "AppServices.h"

#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/library/SignIn.h>
#include <jfd/music/MusicPlayer.h>
#include <jfd/remote/RemoteControl.h>
#include <jfd/remote/RemoteSettings.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/Navigator.h>
#include <jfd/ui/PlayerModel.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>

namespace jfd::desktop {

  // The remote control and the window its toasts and fullscreen act on.
  struct RemoteServices {
    std::unique_ptr<remote::RemoteControl> control;
    QPointer<QQuickWindow> window;
  };

  namespace {
    Q_LOGGING_CATEGORY(lcRemote, "jfd.app.remote")

    using remote::PlayCommand;
    using remote::PlayRequest;

    template <typename... Texts> void callShell(QQuickWindow* window, const char* function, const Texts&... texts) {
      if (window == nullptr) return;
      QObject* shell = window->findChild<QQuickItem*>(QStringLiteral("shell"));
      if (shell != nullptr) QMetaObject::invokeMethod(shell, function, Q_ARG(QVariant, QVariant(texts))...);
    }

    bool musicActive(const AppServices& services) { return services.music && services.music->active(); }

    bool playing(const AppServices& services) {
      if (!services.session) return false;
      const core::PlaybackPhase phase = services.session->snapshot().phase;
      return phase != core::PlaybackPhase::Idle && phase != core::PlaybackPhase::Ended &&
             phase != core::PlaybackPhase::Failed;
    }

    // Tracks go to the music queue, anything else plays in the player.
    // Jellyfin Web and the phone apps send playable items (an album or a
    // series is sent as its tracks or episodes).
    void playItems(AppServices& services, const PlayRequest& request, const QList<api::Item>& items) {
      const bool tracks = std::all_of(items.begin(), items.end(),
                                      [](const api::Item& item) { return item.type == QLatin1String("Audio"); });
      if (tracks && services.music) {
        QList<music::Track> queue;
        for (const api::Item& item : items)
          queue.append(music::Track::fromItem(item));
        music::MusicPlayer& music = *services.music;
        const bool queueing = request.command == PlayCommand::PlayNext || request.command == PlayCommand::PlayLast;
        if (queueing && music.active()) {
          if (request.command == PlayCommand::PlayNext)
            music.playNext(std::move(queue));
          else
            music.append(std::move(queue));
          return;
        }
        const int start = queueing ? 0 : std::clamp(request.startIndex, 0, int(queue.size()) - 1);
        music.play(std::move(queue), start);
        music.setShuffle(request.command == PlayCommand::PlayShuffle);
        return;
      }
      const int start = std::clamp(request.startIndex, 0, int(items.size()) - 1);
      const api::Item& item = items.at(start);
      if (item.isFolder || item.type == QLatin1String("Audio") || !services.playerModel) {
        qCInfo(lcRemote) << "Play: cannot play" << item.type << "here";
        return;
      }
      // The player plays one video at a time; its own queue is the series'
      // next episode.
      if ((request.command == PlayCommand::PlayNext || request.command == PlayCommand::PlayLast) && playing(services)) {
        qCInfo(lcRemote) << "Play: videos are not queued outside a group";
        return;
      }
      services.playerModel->play(item.id, request.command == PlayCommand::PlayNow ? request.startPositionTicks : 0);
      if (services.navigator != nullptr) services.navigator->navigate(QStringLiteral("player"));
    }

    void play(AppServices& services, const PlayRequest& request) {
      api::ItemQuery query;
      query.ids = request.itemIds;
      query.limit = int(request.itemIds.size());
      services.client->items(query, services.client, [&services, request](api::Result<api::ItemPage> page) {
        if (!page) {
          qCWarning(lcRemote) << "Play: the items did not load:" << page.error().message;
          return;
        }
        // In the order asked for; items the user may not see are left out.
        QList<api::Item> items;
        PlayRequest found = request;
        found.startIndex = 0;
        for (int i = 0; i < request.itemIds.size(); ++i) {
          for (const api::Item& item : page->items) {
            if (item.id != request.itemIds.at(i)) continue;
            if (i == request.startIndex) found.startIndex = int(items.size());
            items.append(item);
            break;
          }
        }
        if (items.isEmpty()) {
          qCInfo(lcRemote) << "Play: none of the items was found";
          return;
        }
        playItems(services, found, items);
      });
    }

    void playInGroup(AppServices& services, const PlayRequest& request) {
      if (!services.group) return;
      session::GroupModel& group = *services.group;
      switch (request.command) {
      case PlayCommand::PlayNext: group.queueInGroup(request.itemIds, true); return;
      case PlayCommand::PlayLast: group.queueInGroup(request.itemIds, false); return;
      case PlayCommand::PlayNow:
      case PlayCommand::PlayShuffle:
      case PlayCommand::PlayInstantMix: break;
      }
      group.playInGroup(request.itemIds, request.startIndex, request.startPositionTicks);
      if (services.navigator != nullptr) services.navigator->navigate(QStringLiteral("group"));
    }

    remote::RemoteActions actions(AppServices& services, RemoteServices& remote) {
      remote::RemoteActions a;
      a.play = [&services](const PlayRequest& request) { play(services, request); };
      a.playInGroup = [&services](const PlayRequest& request) { playInGroup(services, request); };
      // The music player keeps its own volume; mute is mpv's, shared by both.
      a.volume = [&services] {
        if (musicActive(services)) return services.music->volume();
        return services.playerModel ? services.playerModel->volume() : 100;
      };
      a.setVolume = [&services](int volume) {
        if (musicActive(services))
          services.music->setVolume(volume);
        else if (services.playerModel)
          services.playerModel->setVolume(volume);
      };
      a.muted = [&services] { return services.playerModel && services.playerModel->muted(); };
      a.setMuted = [&services](bool muted) {
        if (services.playerModel) services.playerModel->setMuted(muted);
      };
      a.selectAudio = [&services](int index) {
        if (services.playerModel) services.playerModel->selectAudio(index);
      };
      a.selectSubtitle = [&services](int index) {
        if (services.playerModel) services.playerModel->selectSubtitle(index);
      };
      a.showMessage = [&remote](const QString& header, const QString& text) {
        const QString message =
            header.isEmpty() ? text : (text.isEmpty() ? header : header + QStringLiteral(": ") + text);
        callShell(remote.window.data(), "showMessage", message);
      };
      a.toggleFullscreen = [&remote] { callShell(remote.window.data(), "toggleFullScreen"); };
      a.goTo = [&services](remote::Destination destination) {
        if (services.navigator == nullptr || services.signIn == nullptr ||
            services.signIn->step() != library::SignIn::Step::SignedIn)
          return;
        switch (destination) {
        case remote::Destination::Home: services.navigator->navigate(QStringLiteral("home")); break;
        case remote::Destination::Search: services.navigator->navigate(QStringLiteral("search")); break;
        case remote::Destination::Settings: services.navigator->navigate(QStringLiteral("settings")); break;
        }
      };
      a.setRepeatMode = [&services](remote::RepeatMode mode) {
        if (!services.music) return;
        services.music->setRepeat(mode == remote::RepeatMode::One   ? QStringLiteral("track")
                                  : mode == remote::RepeatMode::All ? QStringLiteral("queue")
                                                                    : QStringLiteral("none"));
      };
      a.setShuffle = [&services](bool on) {
        if (services.music) services.music->setShuffle(on);
      };
      a.setMaxBitrate = [&services](qint64 bitrate) {
        if (services.playerModel) services.playerModel->setMaxBitrate(bitrate);
      };
      // Navigation (MoveUp ... Back, SendString) waits for the shell's input
      // router (stage 3, package 3.3): until then it is not offered.
      return a;
    }
  } // namespace

  void installRemote(AppServices& services) {
    Q_ASSERT(services.session && services.socket != nullptr);
    services.remote = std::make_shared<RemoteServices>();
    RemoteServices& remote = *services.remote;
    session::SessionRuntime& session = *services.session;
    remote.control = std::make_unique<remote::RemoteControl>(
        [&session](core::SessionInput input) { session.post(std::move(input)); },
        [&session] { return session.snapshot(); }, actions(services, remote));
    remote::RemoteControl& control = *remote.control;
    control.setEnabled(services.settings->value(remote::keys::AllowServerControl).toBool());
    QObject::connect(
        services.socket, &api::SessionSocket::messageReceived, &control,
        [&control](const QString& type, const QJsonValue& data, const QString&) { control.handle(type, data); });
  }

  void applyRemoteCapabilities(const AppServices& services, api::SessionCapabilities& capabilities) {
    if (services.remote && services.remote->control) services.remote->control->applyTo(capabilities);
  }

  void setRemoteEnabled(AppServices& services, bool enabled) {
    if (services.remote && services.remote->control) services.remote->control->setEnabled(enabled);
  }

  void startRemote(AppServices& services, QQuickWindow& window) {
    if (services.remote) services.remote->window = &window;
  }

  bool runCommand(AppServices& services, const QStringList& arguments) {
    const auto text = remote::commandArgument(arguments);
    if (!text) return false;
    const auto command = remote::parseLocalCommand(*text);
    if (!command) {
      // The second start checks the name before it sends; a newer version's
      // command can still reach an older one.
      qCWarning(lcRemote) << "Unknown --command" << *text;
      return true;
    }
    if (services.remote && services.remote->control) services.remote->control->run(*command);
    return true;
  }

} // namespace jfd::desktop
