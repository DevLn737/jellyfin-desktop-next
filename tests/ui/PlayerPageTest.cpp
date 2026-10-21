#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/playback/StreamPlan.h>
#include <jfd/session/GroupModel.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/GroupSession.h>
#include <jfd/ui/PlayerModel.h>
#include <jfd/ui/PlayerSession.h>

#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlextensionplugin.h>

#include <cmath>
#include <memory>

Q_IMPORT_QML_PLUGIN(Jfd_Ui_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ControlsPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_ShellPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_GroupPlugin)
Q_IMPORT_QML_PLUGIN(Jfd_Ui_PlayerPlugin)

// The player page on the session runtime with a virtual backend: no server
// and no mpv. The stream a server would resolve is handed in directly.

using namespace jfd;
using jfd::test::waitFor;

namespace {

  QQuickItem* find(QQuickItem* root, const char* name) {
    if (root->objectName() == QLatin1String(name)) return root;
    for (QQuickItem* child : root->childItems())
      if (QQuickItem* found = find(child, name)) return found;
    return nullptr;
  }

  api::MediaStream stream(int index, const char* type, bool external = false) {
    api::MediaStream s;
    s.index = index;
    s.type = QString::fromLatin1(type);
    s.title = QStringLiteral("%1 %2").arg(s.type).arg(index);
    s.deliveryMethod = external ? QStringLiteral("External") : QString();
    s.isExternal = external;
    return s;
  }

  struct Fixture {
    api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"),
                                                    QStringLiteral("CI"), QStringLiteral("jfd-player-page")}};
    api::SessionSocket socket{client};
    testing::VirtualBackend* backend = nullptr;
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<session::GroupModel> group;
    std::unique_ptr<ui::PlayerModel> player;
    QQmlEngine engine;
    std::unique_ptr<QObject> object;
    QQuickWindow window;
    QQuickItem* page = nullptr;

    Fixture() {
      session::SessionRuntime::Settings settings;
      settings.deviceProfile = api::buildDeviceProfile({});
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [this](const core::Clock& clock, session::SessionRuntime::Post post) {
            auto made = std::make_unique<testing::VirtualBackend>(clock, std::move(post));
            backend = made.get();
            return made;
          },
          settings);
      group = std::make_unique<session::GroupModel>(*runtime, client, socket);
      ui::GroupSession::setInstance(group.get());
      player = std::make_unique<ui::PlayerModel>(*runtime, client, nullptr);
      ui::PlayerSession::setInstance(player.get());

      QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/Jfd/Ui/Player/PlayerPage.qml")));
      object.reset(component.create());
      REQUIRE_MESSAGE(object != nullptr, component.errorString().toStdString());
      page = qobject_cast<QQuickItem*>(object.get());
      REQUIRE(page != nullptr);
      window.resize(1280, 720);
      page->setParentItem(window.contentItem());
      page->setSize(QSizeF(1280, 720));
      window.show();
    }

    // What the launcher emits once the server resolved the item, then the
    // intent it posts.
    void open() {
      session::PlaybackLauncher::Request request;
      request.itemId = QStringLiteral("episode");
      api::MediaSource source;
      source.id = QStringLiteral("source");
      source.defaultAudioStreamIndex = 2;
      source.mediaStreams = {stream(0, "Subtitle", true), stream(1, "Video"), stream(2, "Audio"),
                             stream(3, "Audio"), stream(4, "Subtitle")};
      api::PlaybackStream played;
      played.url = QUrl(QStringLiteral("https://srv/Videos/episode/stream"));
      played.method = api::PlayMethod::DirectPlay;
      played.itemId = request.itemId;
      played.mediaSourceId = source.id;
      api::ExternalTrack subtitle;
      subtitle.streamIndex = 0;
      subtitle.url = QUrl(QStringLiteral("https://srv/Subtitles/0/Stream.srt"));
      played.externalSubtitles = {subtitle};
      api::Item item;
      item.id = request.itemId;
      Q_EMIT runtime->launcher().resolved(request, item, source, played);
      core::MediaRequest media = playback::mediaRequest(played, false, core::MediaTicks(), std::nullopt, false);
      media.title = "Episode";
      runtime->post(core::intent::Open{media});
    }
  };

} // namespace

TEST_CASE("The player page plays, pauses, seeks and lists the file's tracks") {
  Fixture f;
  REQUIRE(find(f.page, "timeline") != nullptr);
  REQUIRE(find(f.page, "playPause") != nullptr);
  CHECK_FALSE(f.player->active());

  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  CHECK(f.player->title() == QStringLiteral("Episode"));
  CHECK(f.player->duration() == doctest::Approx(600.0));
  CHECK(f.player->audioTracks().size() == 2);
  CHECK(f.player->subtitleTracks().size() == 2);
  CHECK(f.player->audioIndex() == 2);
  CHECK(f.player->subtitleIndex() == -1);

  f.player->togglePause();
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  f.player->seek(300);
  REQUIRE(waitFor([&] { return f.player->position() > 299 && f.player->position() < 301; }, 2000));
  f.player->togglePause();
  REQUIRE(waitFor([&] { return !f.player->paused(); }, 2000));

  // The external subtitle is added on first selection, then selected.
  f.player->selectSubtitle(0);
  CHECK(f.player->subtitleIndex() == 0);
  REQUIRE(waitFor([&] { return f.backend->addedTracks.size() == 1; }, 2000));
  CHECK(f.backend->addedTracks.front().url == "https://srv/Subtitles/0/Stream.srt");
  f.player->selectSubtitle(-1);
  CHECK(f.player->subtitleIndex() == -1);

  f.player->stop();
  REQUIRE(waitFor([&] { return !f.player->active() || f.player->ended(); }, 2000));
}

TEST_CASE("Delays are rounded to tenths and reset for the next file") {
  Fixture f;
  f.player->setSubtitleDelay(0.34);
  CHECK(f.player->subtitleDelay() == doctest::Approx(0.3));
  f.player->setAudioDelay(-0.25);
  CHECK(f.player->audioDelay() == doctest::Approx(-0.3));
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading(); }, 5000));
  CHECK(f.player->subtitleDelay() == 0);
  CHECK(f.player->audioDelay() == 0);
}

TEST_CASE("Keys seek, change the volume and the delays, and the speed applies alone") {
  Fixture f;
  f.open();
  REQUIRE(waitFor([&] { return f.player->active() && !f.player->loading() && f.player->position() > 0.2; }, 5000));
  f.page->forceActiveFocus();
  const auto press = [&](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent pressEvent(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&f.window, &pressEvent);
    QKeyEvent releaseEvent(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(&f.window, &releaseEvent);
  };

  f.player->togglePause();
  REQUIRE(waitFor([&] { return f.player->paused(); }, 2000));
  const double before = f.player->position();
  press(Qt::Key_Right);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - (before + 10)) < 0.5; }, 2000));
  press(Qt::Key_Left);
  REQUIRE(waitFor([&] { return std::abs(f.player->position() - before) < 0.5; }, 2000));

  CHECK(f.player->volume() == 100);
  press(Qt::Key_Down);
  press(Qt::Key_Down);
  CHECK(f.player->volume() == 90);
  press(Qt::Key_Up);
  CHECK(f.player->volume() == 95);
  press(Qt::Key_M);
  CHECK(f.player->muted());
  press(Qt::Key_Up);
  CHECK_FALSE(f.player->muted());
  CHECK(f.player->volume() == 100);

  press(Qt::Key_L);
  press(Qt::Key_L);
  press(Qt::Key_J);
  CHECK(f.player->subtitleDelay() == doctest::Approx(0.1));
  CHECK(f.player->audioDelay() == doctest::Approx(0));
  press(Qt::Key_J, Qt::ShiftModifier);
  CHECK(f.player->audioDelay() == doctest::Approx(-0.1));
  CHECK(f.player->subtitleDelay() == doctest::Approx(0.1));

  f.player->setRate(1.5);
  REQUIRE(waitFor([&] { return f.player->baseRate() == doctest::Approx(1.5); }, 2000));
}
