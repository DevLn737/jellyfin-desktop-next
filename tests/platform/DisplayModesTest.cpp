#include "../support/DoctestQt.h"

#include <jfd/platform/DisplayModes.h>

#include <cmath>
#include <limits>
#include <string>

using jfd::platform::chooseDisplayMode;
using jfd::platform::DisplayMode;
using jfd::platform::frameRatesDisagree;
using jfd::platform::ModeChoice;
using Kind = ModeChoice::Kind;

namespace {

  constexpr double Ntsc24 = 24000.0 / 1001.0;  // 23.976
  constexpr double Ntsc30 = 30000.0 / 1001.0;  // 29.97
  constexpr double Ntsc60 = 60000.0 / 1001.0;  // 59.94
  constexpr double Ntsc120 = 120000.0 / 1001.0; // 119.88

  DisplayMode mode(double rate, int width = 1920, int height = 1080, bool interlaced = false) {
    DisplayMode m;
    m.width = width;
    m.height = height;
    m.refreshRate = rate;
    m.interlaced = interlaced;
    return m;
  }

  QList<DisplayMode> modes(std::initializer_list<double> rates) {
    QList<DisplayMode> list;
    for (double rate : rates) list.append(mode(rate));
    return list;
  }

  // The expected outcome: Keep or NoMatch, or the rate to switch to.
  struct Expect {
    Kind kind;
    double rate = 0;
    int multiple = 0;
  };
  Expect keep(int multiple) { return {Kind::Keep, 0, multiple}; }
  Expect none() { return {Kind::NoMatch}; }
  Expect to(double rate, int multiple) { return {Kind::Switch, rate, multiple}; }

  // The frame rates the server reports (RealFrameRate is rounded to three
  // decimals), with the names the rows use.
  struct Fps {
    const char* name;
    double value;
  };
  const Fps FrameRates[] = {{"23.976", 23.976}, {"24", 24},  {"25", 25},       {"29.97", 29.97},
                            {"30", 30},         {"50", 50},  {"59.94", 59.94}, {"60", 60}};

  void checkRow(const char* set, double currentRate, const QList<DisplayMode>& list, const Expect (&expected)[8]) {
    for (std::size_t i = 0; i < 8; ++i) {
      const Fps& fps = FrameRates[i];
      const Expect& want = expected[i];
      const ModeChoice choice = chooseDisplayMode(fps.value, mode(currentRate), list);
      INFO("modes ", set, ", display at ", currentRate, " Hz, video at ", fps.name, " fps");
      CHECK(static_cast<int>(choice.kind) == static_cast<int>(want.kind));
      CHECK(choice.multiple == want.multiple);
      if (want.kind == Kind::Switch) CHECK(choice.mode.refreshRate == doctest::Approx(want.rate).epsilon(1e-6));
      if (want.kind == Kind::Keep) CHECK(choice.mode.refreshRate == doctest::Approx(currentRate));
    }
  }

} // namespace

// The threshold table (stage 3, "Частота: выбор режима"): the display at
// 60 Hz with each set of modes, every common frame rate.
TEST_CASE("Mode choice: a 60 Hz monitor and nothing else") {
  //                        23.976  24      25      29.97   30       50      59.94   60
  const Expect expected[] = {none(), none(), none(), none(), keep(2), none(), none(), keep(1)};
  checkRow("{60}", 60, modes({60}), expected);
}

TEST_CASE("Mode choice: a high refresh monitor (60, 120, 144, 165 Hz)") {
  const QList<DisplayMode> list = modes({60, 120, 144, 165});
  // 23.976 x 5 = 119.88 is 0.1 % from 120: not taken.
  const Expect at60[] = {none(), to(120, 5), none(), none(), keep(2), none(), none(), keep(1)};
  checkRow("{60, 120, 144, 165}", 60, list, at60);
  // The owner's main monitor runs at 165 Hz: 24 goes to 120 (the smallest
  // multiple of 100 Hz or more, not 144), 60 to 60 (the same rate first).
  const Expect at165[] = {none(), to(120, 5), none(), none(), to(120, 4), none(), none(), to(60, 1)};
  checkRow("{60, 120, 144, 165}", 165, list, at165);
  // At 144 Hz, 24 fps already fits (x 6): no switch.
  const Expect at144[] = {none(), keep(6), none(), none(), to(120, 4), none(), none(), to(60, 1)};
  checkRow("{60, 120, 144, 165}", 144, list, at144);
}

TEST_CASE("Mode choice: a monitor with 119.88 Hz beside 120") {
  const QList<DisplayMode> list = modes({60, Ntsc120, 120});
  const Expect at60[] = {to(Ntsc120, 5), to(120, 5),     none(), to(Ntsc120, 4),
                         keep(2),        none(),         to(Ntsc120, 2), keep(1)};
  checkRow("{60, 119.88, 120}", 60, list, at60);
  const Expect at120[] = {to(Ntsc120, 5), keep(5), none(), to(Ntsc120, 4), keep(4), none(), to(Ntsc120, 2), keep(2)};
  checkRow("{60, 119.88, 120}", 120, list, at120);
}

TEST_CASE("Mode choice: a TV (23.976, 24, 50, 59.94, 60 Hz)") {
  const QList<DisplayMode> list = modes({Ntsc24, 24, 50, Ntsc60, 60});
  // 25 and 29.97 have no mode of their own: the largest multiple under
  // 100 Hz (50, 59.94).
  const Expect at60[] = {to(Ntsc24, 1), to(24, 1), to(50, 2), to(Ntsc60, 2), keep(2), to(50, 1), to(Ntsc60, 1), keep(1)};
  checkRow("TV", 60, list, at60);
  const Expect at50[] = {to(Ntsc24, 1), to(24, 1), keep(2), to(Ntsc60, 2), to(60, 2), keep(1), to(Ntsc60, 1), to(60, 1)};
  checkRow("TV", 50, list, at50);
}

TEST_CASE("Mode choice: 23.976 is never played at 24 Hz, nor 24 at 23.976") {
  CHECK(chooseDisplayMode(23.976, mode(60), modes({60, 24})).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(24, mode(60), modes({60, Ntsc24})).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(23.976, mode(24), modes({24, 60})).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(Ntsc24, mode(Ntsc24), modes({Ntsc24, 60})).kind == Kind::Keep);
  // The exact rational and the server's rounded value are the same rate.
  CHECK(chooseDisplayMode(23.976, mode(60), modes({60, Ntsc24})).kind == Kind::Switch);
  CHECK(chooseDisplayMode(Ntsc30, mode(60), modes({60, Ntsc60})).mode.refreshRate == doctest::Approx(Ntsc60));
}

TEST_CASE("Mode choice: rates as X11 and drivers report them") {
  // XRandR: dot clock / (htotal x vtotal); 1080p CEA timings.
  const double x11Ntsc24 = 74175824.0 / (2750.0 * 1125.0);
  const double x11Ntsc60 = 148351648.0 / (2200.0 * 1125.0);
  const double x11At60 = 148500000.0 / (2200.0 * 1125.0);
  const QList<DisplayMode> list = modes({x11At60, x11Ntsc60, x11Ntsc24});
  CHECK(chooseDisplayMode(23.976, mode(x11At60), list).mode.refreshRate == doctest::Approx(x11Ntsc24));
  CHECK(chooseDisplayMode(59.94, mode(x11At60), list).mode.refreshRate == doctest::Approx(x11Ntsc60));
  // A "165 Hz" monitor that runs at 164.997 still fits nothing at 24 fps,
  // and 120 is taken from its list.
  CHECK(chooseDisplayMode(24, mode(164.997), modes({164.997, 119.998, 60})).mode.refreshRate ==
        doctest::Approx(119.998));
}

TEST_CASE("Mode choice: multiples prefer the smallest of 100 Hz or more") {
  CHECK(chooseDisplayMode(25, mode(60), modes({60, 50, 100, 150})).mode.refreshRate == 100);
  CHECK(chooseDisplayMode(24, mode(60), modes({60, 48, 72})).mode.refreshRate == 72);
  CHECK(chooseDisplayMode(24, mode(60), modes({60, 48, 72})).multiple == 3);
  CHECK(chooseDisplayMode(24, mode(60), modes({60, 240, 144, 120})).mode.refreshRate == 120);
  // An exact rate beats any multiple.
  CHECK(chooseDisplayMode(24, mode(60), modes({60, 120, 24})).mode.refreshRate == 24);
}

TEST_CASE("Mode choice: only the current size, never interlaced") {
  QList<DisplayMode> list = modes({60});
  list.append(mode(24, 3840, 2160));
  list.append(mode(24, 1920, 1080, true));
  CHECK(chooseDisplayMode(24, mode(60), list).kind == Kind::NoMatch);
  list.append(mode(24));
  const ModeChoice choice = chooseDisplayMode(24, mode(60), list);
  CHECK(choice.kind == Kind::Switch);
  CHECK(choice.mode.width == 1920);
  CHECK_FALSE(choice.mode.interlaced);
  // An interlaced current mode is never kept.
  CHECK(chooseDisplayMode(30, mode(60, 1920, 1080, true), modes({60})).kind == Kind::Switch);
}

TEST_CASE("Mode choice: frame rates that are not usable change nothing") {
  const QList<DisplayMode> list = modes({60, 120, 24});
  CHECK(chooseDisplayMode(0, mode(60), list).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(-24, mode(60), list).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(301, mode(60), list).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(std::numeric_limits<double>::quiet_NaN(), mode(60), list).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(24, mode(0), list).kind == Kind::NoMatch);
  CHECK(chooseDisplayMode(24, mode(60), {}).kind == Kind::NoMatch);
}

TEST_CASE("Display modes compare by size, scan and rate") {
  CHECK(mode(Ntsc60) == mode(59.9401));
  CHECK_FALSE(mode(60) == mode(Ntsc60));
  CHECK_FALSE(mode(60) == mode(60, 1280, 720));
  CHECK_FALSE(mode(60) == mode(60, 1920, 1080, true));
  CHECK(mode(Ntsc24).toString() == QStringLiteral("1920x1080 @ 23.976 Hz"));
  CHECK(mode(50, 1920, 1080, true).toString() == QStringLiteral("1920x1080i @ 50.000 Hz"));
}

TEST_CASE("The server's and the file's frame rates disagree beyond 1 %") {
  CHECK_FALSE(frameRatesDisagree(23.976, 23.976));
  CHECK_FALSE(frameRatesDisagree(23.976, 24));
  CHECK_FALSE(frameRatesDisagree(25, 25.2));
  CHECK(frameRatesDisagree(25, 50));
  CHECK(frameRatesDisagree(24, 24.3));
  CHECK(frameRatesDisagree(59.94, 29.97));
  CHECK_FALSE(frameRatesDisagree(24, 0));
  CHECK_FALSE(frameRatesDisagree(0, 24));
}

TEST_CASE("The unsupported backend lists nothing and refuses every switch") {
  const auto modes = jfd::platform::createUnsupportedDisplayModes();
  CHECK(modes->backend() == QStringLiteral("none"));
  CHECK_FALSE(modes->isSupported());
  CHECK(modes->screens().isEmpty());
  CHECK(modes->modes(QStringLiteral("DP-1")).isEmpty());
  CHECK_FALSE(modes->currentMode(QStringLiteral("DP-1")).has_value());
  CHECK_FALSE(modes->setMode(QStringLiteral("DP-1"), mode(60)));
}

// CI has no display to switch: the system backend only lists. On Windows
// runners that is the virtual adapter, on Linux Xvfb's RandR (or nothing,
// without $DISPLAY), on Wayland the unsupported backend.
TEST_CASE("The system backend lists the modes of each screen without switching") {
  const auto modes = jfd::platform::createSystemDisplayModes();
  REQUIRE(modes != nullptr);
  MESSAGE("display modes backend: ", modes->backend().toStdString(), ", ",
          std::string(modes->isSupported() ? "supported" : "not supported"));
#ifdef Q_OS_WIN
  CHECK(modes->backend() == QStringLiteral("windows"));
  CHECK(modes->isSupported());
#endif
  if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) CHECK_FALSE(modes->isSupported());
  // The cheap check agrees whenever the backend works.
  if (modes->isSupported()) CHECK(jfd::platform::displayModesAvailable());
  if (!modes->isSupported()) {
    CHECK(modes->screens().isEmpty());
    return;
  }
  const QStringList screens = modes->screens();
  for (const QString& screen : screens) {
    const QList<DisplayMode> list = modes->modes(screen);
    const auto current = modes->currentMode(screen);
    std::string listed;
    for (const DisplayMode& m : list) listed += " [" + m.toString().toStdString() + "]";
    MESSAGE(screen.toStdString(), ": current ", current ? current->toString().toStdString() : std::string("none"),
            ", modes", listed);
    REQUIRE(current.has_value());
    CHECK(current->width > 0);
    CHECK(current->height > 0);
    CHECK_FALSE(list.isEmpty());
    bool sizeListed = false;
    for (const DisplayMode& m : list) sizeListed = sizeListed || (m.width == current->width && m.height == current->height);
    CHECK(sizeListed);
  }
  CHECK_FALSE(modes->currentMode(QStringLiteral("no-such-screen")).has_value());
  CHECK(modes->modes(QStringLiteral("no-such-screen")).isEmpty());
}
