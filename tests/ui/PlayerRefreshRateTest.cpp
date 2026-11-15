#include "../support/DoctestQt.h"
#include "../support/FakeServer.h"
#include "../support/VirtualBackend.h"
#include "platform/FakeDisplayModes.h"

#include <jfd/api/DeviceProfile.h>
#include <jfd/api/JellyfinClient.h>
#include <jfd/api/SessionSocket.h>
#include <jfd/platform/DisplayModeSwitcher.h>
#include <jfd/session/SessionRuntime.h>
#include <jfd/ui/PlayerModel.h>

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include <memory>

// The player screen's model switching the display's refresh rate (stage 3,
// package 3.5) on a fake display, through the session runtime's real
// launcher against a fake server: switch, wait, then open; the original
// rate back on every way out.

using namespace jfd;
using jfd::test::FakeDisplayModes;
using jfd::test::waitFor;

namespace {

  constexpr double Ntsc120 = 120000.0 / 1001.0;

  // RealFrameRate per item: "film" 23.976, "film24" 24, "film60" 60,
  // "noRate" none.
  QByteArray playbackInfo(const QString& itemId) {
    QString video = QStringLiteral(R"({"Index": 0, "Type": "Video", "Codec": "h264")");
    if (itemId == QLatin1String("film")) video += QStringLiteral(R"(, "RealFrameRate": 23.976)");
    if (itemId == QLatin1String("film24")) video += QStringLiteral(R"(, "RealFrameRate": 24)");
    if (itemId == QLatin1String("film60")) video += QStringLiteral(R"(, "RealFrameRate": 60)");
    video += QLatin1Char('}');
    return QStringLiteral(R"({"MediaSources": [{"Id": "src-%1", "Protocol": "File", "Path": "/media/%1.mkv",
        "Container": "mkv", "SupportsDirectPlay": true, "SupportsDirectStream": true,
        "RunTimeTicks": 72000000000, "MediaStreams": [%2, {"Index": 1, "Type": "Audio", "Codec": "aac"}]}],
        "PlaySessionId": "ps-%1"})")
        .arg(itemId, video)
        .toUtf8();
  }

  struct Fixture {
    test::FakeServer server{[](const test::HttpRequest& request) {
      test::HttpResponse response;
      const QStringList parts = request.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
      if (parts.size() == 3 && parts[0] == QLatin1String("Items") && parts[2] == QLatin1String("PlaybackInfo")) {
        response.body = playbackInfo(parts[1]);
      } else if (parts.size() == 2 && parts[0] == QLatin1String("Items")) {
        response.body = QStringLiteral(R"({"Id":"%1","Name":"%1","Type":"Movie","MediaType":"Video"})")
                            .arg(parts[1])
                            .toUtf8();
      } else {
        // Reports, segments, trickplay: nothing to say.
        response.status = 204;
      }
      return response;
    }};
    api::JellyfinClient client{api::ClientIdentity{QStringLiteral("Tests"), QStringLiteral("0.0.1"),
                                                    QStringLiteral("CI"), QStringLiteral("jfd-player-refresh")}};
    api::SessionSocket socket{client};
    QTemporaryDir dir;
    FakeDisplayModes modes;
    std::unique_ptr<platform::DisplayModeSwitcher> switcher;
    std::unique_ptr<session::SessionRuntime> runtime;
    std::unique_ptr<ui::PlayerModel> player;
    QWindow window;
    QString screen;
    // In order: "switch <rate>", "confirmed", "open <item>".
    QStringList events;

    Fixture() {
      REQUIRE(dir.isValid());
      client.setServerUrl(server.url());
      session::SessionRuntime::Settings settings;
      settings.deviceProfile = api::buildDeviceProfile({});
      runtime = std::make_unique<session::SessionRuntime>(
          client, socket,
          [](const core::Clock& clock, session::SessionRuntime::Post post) {
            return std::make_unique<testing::VirtualBackend>(clock, std::move(post));
          },
          settings);
      player = std::make_unique<ui::PlayerModel>(*runtime, client, nullptr);
      QObject::connect(&runtime->launcher(), &session::PlaybackLauncher::resolved,
                       [this](const session::PlaybackLauncher::Request& request) {
                         events << QStringLiteral("open ") + request.itemId;
                       });

      window.resize(640, 360);
      window.showFullScreen();
      REQUIRE(window.visibility() == QWindow::FullScreen);
      // The offscreen platform's screen may have no name; the model passes
      // whatever Qt reports.
      screen = window.screen()->name();
      // A monitor at 60 Hz that also has 119.88 and 120.
      modes.addScreen(screen, 60, {60, Ntsc120, 120});
      modes.switchDelayMs = 50;
      modes.beforeSwitch = [this](const QString&, const platform::DisplayMode& mode) {
        events << QStringLiteral("switch %1").arg(mode.refreshRate, 0, 'f', 2);
      };
      QObject::connect(&modes, &platform::DisplayModes::modeChanged, [this] { events << QStringLiteral("confirmed"); });
      switcher = std::make_unique<platform::DisplayModeSwitcher>(modes, journal());
      player->setDisplayModeSwitcher(switcher.get());
      player->setRefreshRateSwitch(true);
      player->setVideoWindow(&window);
    }

    ~Fixture() {
      // The model first: it restores through the switcher.
      player.reset();
      runtime.reset();
    }

    QString journal() const { return dir.filePath(QStringLiteral("display-mode.json")); }
    double rate() const { return modes.rate(screen); }
    bool playing(const char* item) const {
      return player->active() && !player->loading() && player->title() == QLatin1String(item);
    }
    // Plays and waits for the file to run.
    void play(const char* item) {
      player->play(QLatin1String(item));
      REQUIRE(waitFor([&] { return playing(item); }, 8000));
    }
  };

} // namespace

TEST_CASE("Refresh rate: the display switches, settles, then the file opens") {
  Fixture f;
  f.player->play(QStringLiteral("film"));
  // Resolved and switching: the file is not open, the page shows loading.
  REQUIRE(waitFor([&] { return f.events.contains(QStringLiteral("switch 119.88")); }, 5000));
  CHECK(f.player->loading());
  CHECK(f.player->active());
  CHECK(f.runtime->snapshot().phase == core::PlaybackPhase::Idle);
  CHECK(QFile::exists(f.journal()));

  REQUIRE(waitFor([&] { return f.playing("film"); }, 8000));
  CHECK(f.events == QStringList{QStringLiteral("switch 119.88"), QStringLiteral("confirmed"),
                                QStringLiteral("open film")});
  CHECK(f.rate() == doctest::Approx(Ntsc120));
}

TEST_CASE("Refresh rate: the next file switches from the original rate") {
  Fixture f;
  f.play("film");
  CHECK(f.rate() == doctest::Approx(Ntsc120));
  f.events.clear();
  f.play("film24");
  CHECK(f.events == QStringList{QStringLiteral("switch 120.00"), QStringLiteral("confirmed"),
                                QStringLiteral("open film24")});
  // 60 fps fits the original 60 Hz: back to it before the file opens.
  f.events.clear();
  f.play("film60");
  CHECK(f.events == QStringList{QStringLiteral("switch 60.00"), QStringLiteral("confirmed"),
                                QStringLiteral("open film60")});
  CHECK_FALSE(f.switcher->isSwitched());
  CHECK_FALSE(QFile::exists(f.journal()));
}

TEST_CASE("Refresh rate: nothing switches when off, in a window or without a frame rate") {
  SUBCASE("setting off") {
    Fixture f;
    f.player->setRefreshRateSwitch(false);
    f.play("film");
    CHECK(f.modes.switches.isEmpty());
  }
  SUBCASE("in a window") {
    Fixture f;
    f.window.showNormal();
    f.play("film");
    CHECK(f.modes.switches.isEmpty());
  }
  SUBCASE("no RealFrameRate") {
    Fixture f;
    f.play("noRate");
    CHECK(f.modes.switches.isEmpty());
  }
  SUBCASE("no switcher (unsupported system)") {
    Fixture f;
    f.player->setDisplayModeSwitcher(nullptr);
    f.play("film");
    CHECK(f.modes.switches.isEmpty());
  }
}

TEST_CASE("Refresh rate: going full screen during a film switches nothing") {
  Fixture f;
  f.window.showNormal();
  f.play("film");
  f.window.showFullScreen();
  QTest::qWait(200);
  CHECK(f.modes.switches.isEmpty());
  CHECK(f.rate() == 60);
}

TEST_CASE("Refresh rate: the original rate comes back on every way out") {
  SUBCASE("stop") {
    Fixture f;
    f.play("film");
    f.player->stop();
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
  }
  SUBCASE("leaving full screen") {
    Fixture f;
    f.play("film");
    f.window.showNormal();
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
    // Playback goes on.
    CHECK(f.playing("film"));
  }
  SUBCASE("picture-in-picture") {
    Fixture f;
    f.play("film");
    f.player->enterPip();
    CHECK(f.player->pip());
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
  }
  SUBCASE("quitting") {
    Fixture f;
    f.play("film");
    f.player.reset();
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
  }
  SUBCASE("stopped by the session (a group, a remote)") {
    Fixture f;
    f.play("film");
    f.runtime->post(core::intent::Stop{});
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
  }
  SUBCASE("the setting turned off") {
    Fixture f;
    f.play("film");
    f.player->setRefreshRateSwitch(false);
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
  }
  SUBCASE("stopped while the display settles") {
    Fixture f;
    f.modes.confirms = false;
    f.switcher->setSettleMs(1000);
    f.player->play(QStringLiteral("film"));
    REQUIRE(waitFor([&] { return f.modes.switches.size() == 1; }, 5000));
    f.player->stop();
    CHECK_FALSE(f.player->loading());
    QTest::qWait(1300);
    // The file never opened; the original rate was asked for again.
    CHECK_FALSE(f.events.contains(QStringLiteral("open film")));
    REQUIRE(f.modes.switches.size() == 2);
    CHECK(f.modes.switches.back().mode.refreshRate == 60);
  }
  SUBCASE("a crash: the next start restores from the journal") {
    Fixture f;
    f.play("film");
    // What the crashed run leaves: the journal and the display at 119.88.
    REQUIRE(QFile::copy(f.journal(), f.journal() + QStringLiteral(".kept")));
    f.player.reset();
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
    f.modes.current[f.screen] = FakeDisplayModes::mode(Ntsc120);
    REQUIRE(QFile::rename(f.journal() + QStringLiteral(".kept"), f.journal()));
    // As installPlayer does at start.
    platform::DisplayModeSwitcher next(f.modes, f.journal());
    CHECK(next.restorePending());
    REQUIRE(waitFor([&] { return f.rate() == 60; }, 3000));
    CHECK_FALSE(QFile::exists(f.journal()));
  }
}

TEST_CASE("Refresh rate: a display that never confirms opens after the settle time") {
  Fixture f;
  f.modes.confirms = false;
  f.switcher->setSettleMs(300);
  QElapsedTimer waited;
  waited.start();
  f.player->play(QStringLiteral("film"));
  REQUIRE(waitFor([&] { return f.events.contains(QStringLiteral("open film")); }, 5000));
  // Qt's coarse timers may fire up to 5 % early.
  CHECK(waited.elapsed() >= 280);
  CHECK(f.events.first() == QStringLiteral("switch 119.88"));
}
