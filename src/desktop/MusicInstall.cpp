#include "AppServices.h"
#include "IntegrationServices.h"

#include <jfd/library/SignIn.h>
#include <jfd/music/MusicPlayer.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/settings/Settings.h>
#include <jfd/settings/SettingsRegistry.h>
#include <jfd/ui/MusicUi.h>
#include <jfd/ui/Navigator.h>

#include <MpvPlayer.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QScreen>
#include <QUrl>

namespace jfd::desktop {

  namespace {
    const QString VolumeKey = QStringLiteral("music/defaultVolume");
    const QString NormalizeKey = QStringLiteral("music/normalization");
    const QString ContinueKey = QStringLiteral("music/continueQueue");
    const QString QueueKey = QStringLiteral("music/queue");
    const QString ModuleUrl = QStringLiteral("qrc:/qt/qml/Jfd/Ui/Music/");

    void registerPageRoute(ui::Navigator& navigator, const QString& name, const QString& page, bool takesArgument,
                           const ui::RouteSpec::SidebarEntry* sidebar = nullptr) {
      ui::RouteSpec spec;
      spec.name = name;
      spec.takesArgument = takesArgument;
      spec.page = QUrl(ModuleUrl + page);
      if (sidebar != nullptr) {
        spec.hasSidebarEntry = true;
        spec.sidebar = *sidebar;
      }
      navigator.registerRoute(spec);
    }

    // ReplayGain tags of the file (direct play); the server's gain for files
    // without them.
    void applyNormalization(media::MpvPlayer& player, bool on, const music::Track& track) {
      player.setStringProperty("replaygain", on ? "track" : "no");
      player.setDoubleProperty("replaygain-fallback", on ? track.normalizationGain.value_or(0.0) : 0.0);
    }
  } // namespace

  bool registerMusicSettings(settings::SettingsRegistry& registry) {
    settings::SectionDescriptor section;
    section.id = QStringLiteral("music");
    section.context = "MusicSettings";
    section.title = QT_TRANSLATE_NOOP("MusicSettings", "Music");
    section.order = 105;

    settings::SettingDescriptor volume;
    volume.key = QStringLiteral("defaultVolume");
    volume.type = settings::SettingType::Int;
    volume.defaultValue = 100;
    volume.minimum = 0;
    volume.maximum = 100;
    volume.step = 5;
    volume.unit = "%";
    volume.label = QT_TRANSLATE_NOOP("MusicSettings", "Volume at start");
    volume.description = QT_TRANSLATE_NOOP("MusicSettings", "The music volume when the app starts.");
    section.settings.append(volume);

    settings::SettingDescriptor normalization;
    normalization.key = QStringLiteral("normalization");
    normalization.type = settings::SettingType::Bool;
    normalization.defaultValue = true;
    normalization.label = QT_TRANSLATE_NOOP("MusicSettings", "Even out loudness between tracks");
    normalization.description = QT_TRANSLATE_NOOP(
        "MusicSettings",
        "Uses the ReplayGain tags of the files; for files without them, the gain the server worked out. "
        "Tracks the server converts to another format are not changed.");
    section.settings.append(normalization);

    settings::SettingDescriptor keep;
    keep.key = QStringLiteral("continueQueue");
    keep.type = settings::SettingType::Bool;
    keep.defaultValue = true;
    keep.label = QT_TRANSLATE_NOOP("MusicSettings", "Remember the queue");
    keep.description = QT_TRANSLATE_NOOP(
        "MusicSettings", "The queue and the place in the track stay when the app is closed; Play picks them up.");
    section.settings.append(keep);

    // The queue itself, saved by the player.
    settings::SettingDescriptor queue;
    queue.key = QStringLiteral("queue");
    queue.type = settings::SettingType::String;
    queue.defaultValue = QString();
    queue.label = "queue";
    queue.hidden = true;
    section.settings.append(queue);
    return registry.registerSection(std::move(section));
  }

  void installMusic(AppServices& services, QQmlEngine&) {
    Q_ASSERT(services.session && services.client != nullptr && services.navigator != nullptr);
    settings::Settings& settings = *services.settings;
    services.music = std::make_unique<music::MusicPlayer>(*services.session, *services.client);
    music::MusicPlayer& player = *services.music;

    media::MpvPlayer* mpv = services.player.get();
    if (mpv != nullptr) {
      player.setOutputHooks(
          [mpv](int volume) { mpv->setDoubleProperty("volume", volume); },
          [mpv, &settings](const music::Track& track) {
            applyNormalization(*mpv, settings.value(NormalizeKey).toBool(), track);
          });
    }
    player.setVolume(settings.value(VolumeKey).toInt());
    QObject::connect(&settings, &settings::Settings::valueChanged, &player,
                     [&player](const QString& key, const QVariant& value) {
                       if (key == VolumeKey) player.setVolume(value.toInt());
                     });

    // The saved queue belongs to a server and user: it comes back once the
    // client is signed in as them.
    const auto restore = [&player, &settings] {
      if (player.queueSize() == 0 && settings.value(ContinueKey).toBool())
        player.restoreState(settings.value(QueueKey).toString().toUtf8());
    };
    player.setPersistence([&settings](const QByteArray& state) {
      if (settings.value(ContinueKey).toBool()) settings.setValue(QueueKey, QString::fromUtf8(state));
      else settings.setValue(QueueKey, QString());
    });
    if (services.signIn != nullptr) {
      QObject::connect(services.signIn, &library::SignIn::signedIn, &player, restore);
      if (services.signIn->step() == library::SignIn::Step::SignedIn) restore();
    }

    // The media controls' next and previous follow the music queue.
    if (services.integrations) {
      services.integrations->completeDetails = [&player](integrations::NowPlayingDetails& details) {
        if (details.kind != integrations::PresenceInfo::Kind::Music) return;
        details.hasNext = player.hasNext();
        details.hasPrevious = player.hasPrevious();
      };
      const auto refresh = [&services] { refreshDetails(services); };
      QObject::connect(&player, &music::MusicPlayer::queueChanged, &player, refresh);
      QObject::connect(&player, &music::MusicPlayer::currentChanged, &player, refresh);
    }

    ui::MusicUi::Config config;
    config.client = services.client;
    config.player = &player;
    if (const QScreen* screen = QGuiApplication::primaryScreen())
      config.devicePixelRatio = screen->devicePixelRatio();
    services.musicUi = std::make_unique<ui::MusicUi>(config);
    ui::Music::setInstance(services.musicUi.get());
    ui::MusicSession::setInstance(&player);

    ui::Navigator& navigator = *services.navigator;
    ui::RouteSpec::SidebarEntry entry;
    entry.icon = ModuleUrl + QStringLiteral("icons/music.svg");
    entry.text = QString::fromUtf8(QT_TRANSLATE_NOOP("MusicNav", "Music"));
    entry.translationContext = "MusicNav";
    entry.order = 150;
    registerPageRoute(navigator, QStringLiteral("music"), QStringLiteral("MusicPage.qml"), false, &entry);
    registerPageRoute(navigator, QStringLiteral("album"), QStringLiteral("AlbumPage.qml"), true);
    registerPageRoute(navigator, QStringLiteral("artist"), QStringLiteral("ArtistPage.qml"), true);
    registerPageRoute(navigator, QStringLiteral("playlist"), QStringLiteral("PlaylistPage.qml"), true);
    registerPageRoute(navigator, QStringLiteral("genre"), QStringLiteral("GenrePage.qml"), true);
    registerPageRoute(navigator, QStringLiteral("nowplaying"), QStringLiteral("NowPlayingPage.qml"), false);

    // The sidebar lists Music once the server has a music library.
    navigator.setRouteAvailable(QStringLiteral("music"), services.musicUi->available());
    QObject::connect(services.musicUi.get(), &ui::MusicUi::availableChanged, &navigator,
                     [&navigator, ui = services.musicUi.get()] {
                       navigator.setRouteAvailable(QStringLiteral("music"), ui->available());
                     });
  }

  void installMiniPlayer(AppServices& services, QQmlEngine& engine) {
    auto* app = qobject_cast<QQmlApplicationEngine*>(&engine);
    if (app == nullptr || app->rootObjects().isEmpty()) return;
    QObject* shell = app->rootObjects().first()->findChild<QObject*>(QStringLiteral("shell"));
    if (shell == nullptr) return;
    auto* slot = shell->property("miniPlayerSlot").value<QQuickItem*>();
    if (slot == nullptr) return;
    QQmlComponent component(&engine, QUrl(ModuleUrl + QStringLiteral("MiniPlayer.qml")));
    auto* mini = qobject_cast<QQuickItem*>(component.create());
    if (mini == nullptr) {
      qWarning() << "The mini-player did not load:" << component.errorString();
      return;
    }
    mini->setParent(slot);
    mini->setParentItem(slot);
    Q_UNUSED(services);
  }

  void uninstallMusic(AppServices& services) {
    ui::MusicSession::setInstance(nullptr);
    ui::Music::setInstance(nullptr);
    services.musicUi.reset();
    services.music.reset();
  }

} // namespace jfd::desktop
