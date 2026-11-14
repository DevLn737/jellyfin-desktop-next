#include "../support/DoctestQt.h"
#include "FakeDisplayModes.h"
#include "Wait.h"

#include <jfd/platform/DisplayModeSwitcher.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

using jfd::platform::DisplayModeSwitcher;
using jfd::platform::ModeChoice;
using jfd::test::FakeDisplayModes;
using jfd::test::spinUntil;

namespace {

  const QString Main = QStringLiteral("DP-1");
  const QString Second = QStringLiteral("HDMI-1");

  struct Fixture {
    QTemporaryDir dir;
    FakeDisplayModes modes;
    std::unique_ptr<DisplayModeSwitcher> switcher;

    Fixture() {
      REQUIRE(dir.isValid());
      // The owner's two monitors: 165 Hz and 60 Hz.
      modes.addScreen(Main, 165, {60, 120, 144, 165});
      modes.addScreen(Second, 60, {50, 60});
      modes.switchDelayMs = 30;
      switcher = std::make_unique<DisplayModeSwitcher>(modes, journal());
    }

    QString journal() const { return dir.filePath(QStringLiteral("display-mode.json")); }
    QJsonObject readJournal() const {
      QFile file(journal());
      if (!file.open(QIODevice::ReadOnly)) return {};
      return QJsonDocument::fromJson(file.readAll()).object();
    }
  };

} // namespace

TEST_CASE("A switch writes the journal first, then opens once the system confirms the mode") {
  Fixture f;
  bool ready = false;
  bool journalBeforeSwitch = false;
  // The journal must exist by the time the system is asked.
  f.modes.beforeSwitch = [&](const QString&, const jfd::platform::DisplayMode&) {
    journalBeforeSwitch = QFile::exists(f.journal());
  };
  QSignalSpy switched(f.switcher.get(), &DisplayModeSwitcher::switchedChanged);
  const ModeChoice choice = f.switcher->prepare(Main, 24, [&] { ready = true; });
  CHECK(choice.kind == ModeChoice::Kind::Switch);
  CHECK(choice.mode.refreshRate == 120);
  CHECK_FALSE(ready);
  CHECK(f.switcher->isWaiting());
  CHECK(f.switcher->isSwitched());
  CHECK(f.switcher->switchedScreen() == Main);
  CHECK(switched.count() == 1);
  REQUIRE(f.modes.switches.size() == 1);
  CHECK(QFile::exists(f.journal()));
  const QJsonObject journal = f.readJournal();
  CHECK(journal.value(QStringLiteral("screen")).toString() == Main);
  CHECK(journal.value(QStringLiteral("refreshRate")).toDouble() == 165);
  CHECK(journal.value(QStringLiteral("backend")).toString() == QStringLiteral("fake"));

  QElapsedTimer waited;
  waited.start();
  REQUIRE(spinUntil([&] { return ready; }, 2000));
  // On the system's notice, long before the 2 s settle time.
  CHECK(waited.elapsed() < 1500);
  CHECK(journalBeforeSwitch);
  CHECK(f.modes.rate(Main) == 120);
  CHECK_FALSE(f.switcher->isWaiting());

  f.switcher->restore();
  CHECK_FALSE(f.switcher->isSwitched());
  CHECK(switched.count() == 2);
  REQUIRE(spinUntil([&] { return f.modes.rate(Main) == 165; }, 2000));
  CHECK_FALSE(QFile::exists(f.journal()));
}

TEST_CASE("Nothing changes when the current mode fits or nothing fits") {
  Fixture f;
  int ready = 0;
  // 24 fps on the second monitor at 60 Hz: nothing fits.
  CHECK(f.switcher->prepare(Second, 24, [&] { ++ready; }).kind == ModeChoice::Kind::NoMatch);
  CHECK(ready == 1);
  // 30 fps at 60 Hz: already fits.
  CHECK(f.switcher->prepare(Second, 30, [&] { ++ready; }).kind == ModeChoice::Kind::Keep);
  CHECK(ready == 2);
  CHECK(f.modes.switches.isEmpty());
  CHECK_FALSE(f.switcher->isSwitched());
  CHECK_FALSE(QFile::exists(f.journal()));
}

TEST_CASE("Without the system's notice the video opens after the settle time") {
  Fixture f;
  f.modes.confirms = false;
  f.switcher->setSettleMs(150);
  bool ready = false;
  QElapsedTimer waited;
  waited.start();
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  CHECK_FALSE(ready);
  REQUIRE(spinUntil([&] { return ready; }, 3000));
  CHECK(waited.elapsed() >= 140);
  CHECK(f.switcher->isSwitched());
}

TEST_CASE("The settle time stays between 0 and 5 seconds") {
  Fixture f;
  CHECK(f.switcher->settleMs() == DisplayModeSwitcher::DefaultSettleMs);
  f.switcher->setSettleMs(9000);
  CHECK(f.switcher->settleMs() == 5000);
  f.switcher->setSettleMs(-5);
  CHECK(f.switcher->settleMs() == 0);
}

TEST_CASE("A refused switch opens at once and leaves no journal") {
  Fixture f;
  f.modes.refuses = true;
  bool ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  CHECK(ready);
  CHECK_FALSE(f.switcher->isSwitched());
  CHECK_FALSE(QFile::exists(f.journal()));
}

TEST_CASE("Where modes cannot change, every video opens at once") {
  Fixture f;
  f.modes.supported = false;
  bool ready = false;
  CHECK(f.switcher->prepare(Main, 24, [&] { ready = true; }).kind == ModeChoice::Kind::NoMatch);
  CHECK(ready);
  CHECK(f.modes.switches.isEmpty());
  // A screen the backend does not know: the same.
  f.modes.supported = true;
  ready = false;
  f.switcher->prepare(QStringLiteral("DP-9"), 24, [&] { ready = true; });
  CHECK(ready);
  CHECK(f.modes.switches.isEmpty());
}

TEST_CASE("The next video chooses from the original mode") {
  Fixture f;
  bool ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  CHECK(f.modes.rate(Main) == 120);

  // 60 fps: the original 165 does not fit, 60 does.
  ready = false;
  f.switcher->prepare(Main, 60, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  CHECK(f.modes.rate(Main) == 60);
  CHECK(f.switcher->isSwitched());
  CHECK(f.readJournal().value(QStringLiteral("refreshRate")).toDouble() == 165);

  // 25 fps: nothing fits, back to the original; nothing left to restore.
  ready = false;
  f.switcher->prepare(Main, 25, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  CHECK(f.modes.rate(Main) == 165);
  CHECK_FALSE(f.switcher->isSwitched());
  CHECK_FALSE(QFile::exists(f.journal()));

  // 24 again, then 24 again: the second one is already in place.
  ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  const auto switches = f.modes.switches.size();
  ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  CHECK(ready);
  CHECK(f.modes.switches.size() == switches);
}

TEST_CASE("A video on the other screen puts the first one back") {
  Fixture f;
  bool ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  ready = false;
  f.switcher->prepare(Second, 25, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  CHECK(f.modes.rate(Main) == 165);
  CHECK(f.modes.rate(Second) == 50);
  CHECK(f.switcher->switchedScreen() == Second);
  CHECK(f.readJournal().value(QStringLiteral("screen")).toString() == Second);
  CHECK(f.readJournal().value(QStringLiteral("refreshRate")).toDouble() == 60);
}

TEST_CASE("Cancelling or restoring drops the video still waiting") {
  Fixture f;
  f.modes.confirms = false;
  f.switcher->setSettleMs(50);
  bool ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  f.switcher->cancel();
  CHECK_FALSE(f.switcher->isWaiting());
  CHECK(f.switcher->isSwitched());
  spinUntil([] { return false; }, 150);
  CHECK_FALSE(ready);

  f.switcher->prepare(Main, 24, [&] { ready = true; });
  f.switcher->restore();
  spinUntil([] { return false; }, 150);
  CHECK_FALSE(ready);
  CHECK_FALSE(f.switcher->isSwitched());
  CHECK_FALSE(QFile::exists(f.journal()));
}

TEST_CASE("A switch the system has not confirmed yet is still put back") {
  Fixture f;
  f.modes.switchDelayMs = 100;
  f.switcher->prepare(Main, 24, [] {});
  f.switcher->restore();
  // The switch lands after the restore was asked; the restore lands last.
  REQUIRE(f.modes.switches.size() == 2);
  CHECK(f.modes.switches.back().mode.refreshRate == 165);
  REQUIRE(spinUntil([&] { return f.modes.switches.size() == 2 && f.modes.rate(Main) == 165; }, 2000));
  spinUntil([] { return false; }, 250);
  CHECK(f.modes.rate(Main) == 165);
}

TEST_CASE("Destroying the switcher (quitting) puts the mode back") {
  Fixture f;
  bool ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  f.switcher.reset();
  REQUIRE(spinUntil([&] { return f.modes.rate(Main) == 165; }));
  CHECK_FALSE(QFile::exists(f.journal()));
}

TEST_CASE("After a crash the next start restores the mode from the journal") {
  Fixture f;
  bool ready = false;
  f.switcher->prepare(Main, 24, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }));
  // What a crashed run leaves: the display switched and the journal.
  QFile::copy(f.journal(), f.journal() + QStringLiteral(".kept"));
  f.switcher.reset();
  REQUIRE(spinUntil([&] { return f.modes.rate(Main) == 165; }));
  f.modes.current[Main] = FakeDisplayModes::mode(120);
  REQUIRE(QFile::rename(f.journal() + QStringLiteral(".kept"), f.journal()));

  DisplayModeSwitcher next(f.modes, f.journal());
  CHECK(next.restorePending());
  REQUIRE(spinUntil([&] { return f.modes.rate(Main) == 165; }));
  CHECK_FALSE(QFile::exists(f.journal()));
  CHECK_FALSE(next.isSwitched());
  // Once is enough.
  CHECK_FALSE(next.restorePending());
}

TEST_CASE("A journal that cannot be applied is removed without switching") {
  Fixture f;
  const auto write = [&](const QByteArray& content) {
    QFile file(f.journal());
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(content);
  };
  CHECK_FALSE(f.switcher->restorePending());

  write("not json");
  CHECK_FALSE(f.switcher->restorePending());
  CHECK_FALSE(QFile::exists(f.journal()));

  // Written under X11, read in a Wayland session.
  write(R"({"version":1,"backend":"xrandr","screen":"DP-1","width":1920,"height":1080,"refreshRate":60,"id":"0"})");
  CHECK_FALSE(f.switcher->restorePending());
  CHECK_FALSE(QFile::exists(f.journal()));

  // The screen is gone.
  write(R"({"version":1,"backend":"fake","screen":"DP-9","width":1920,"height":1080,"refreshRate":60,"id":"0"})");
  CHECK_FALSE(f.switcher->restorePending());
  CHECK_FALSE(QFile::exists(f.journal()));

  // Already back (the system did it).
  write(R"({"version":1,"backend":"fake","screen":"DP-1","width":1920,"height":1080,"refreshRate":165,"id":"0"})");
  CHECK_FALSE(f.switcher->restorePending());
  CHECK(f.modes.switches.isEmpty());
  CHECK_FALSE(QFile::exists(f.journal()));
}

TEST_CASE("A journal that cannot be written means no switch") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  FakeDisplayModes modes;
  modes.addScreen(Main, 60, {60, 120});
  // A directory where the file should be.
  const QString path = dir.filePath(QStringLiteral("display-mode.json"));
  REQUIRE(QDir().mkpath(path));
  DisplayModeSwitcher switcher(modes, path);
  bool ready = false;
  switcher.prepare(Main, 24, [&] { ready = true; });
  CHECK(ready);
  CHECK(modes.switches.isEmpty());
  CHECK_FALSE(switcher.isSwitched());
}
