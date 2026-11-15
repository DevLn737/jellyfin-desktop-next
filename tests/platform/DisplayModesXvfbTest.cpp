#include "../support/DoctestQt.h"
#include "Wait.h"

#include <jfd/platform/DisplayModeSwitcher.h>
#include <jfd/platform/DisplayModes.h>

#include <QFile>
#include <QTemporaryDir>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cstring>
#include <string>

// The XRandR backend against a real X server: Xvfb, which CI runs the tests
// under (xvfb-run). Xvfb has one output, "screen", that takes modes added
// at run time, so a switch to 23.976 Hz and back really happens there. On
// any other X server (a developer's desktop) the test switches nothing.

using jfd::platform::DisplayMode;
using jfd::platform::DisplayModeSwitcher;
using jfd::test::spinUntil;

namespace {

  const QString XvfbOutput = QStringLiteral("screen");

  // Adds a 1920x1080 mode with CEA timings at dotClock / (2750 x 1125) Hz
  // (the 24 Hz family) or dotClock / (2200 x 1125) (60 Hz) to Xvfb's output.
  bool addMode(Display* display, const char* name, unsigned long dotClock, unsigned hTotal) {
    Window root = DefaultRootWindow(display);
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display, root);
    if (!resources || resources->noutput != 1) {
      if (resources) XRRFreeScreenResources(resources);
      return false;
    }
    XRRModeInfo info;
    std::memset(&info, 0, sizeof info);
    info.width = 1920;
    info.height = 1080;
    info.dotClock = dotClock;
    info.hSyncStart = 2008;
    info.hSyncEnd = 2052;
    info.hTotal = hTotal;
    info.vSyncStart = 1084;
    info.vSyncEnd = 1089;
    info.vTotal = 1125;
    std::string modeName = name;
    info.name = modeName.data();
    info.nameLength = static_cast<unsigned>(modeName.size());
    const RRMode mode = XRRCreateMode(display, root, &info);
    XRRAddOutputMode(display, resources->outputs[0], mode);
    XSync(display, False);
    XRRFreeScreenResources(resources);
    return mode != 0;
  }

  // True on Xvfb: a single output called "screen".
  bool onXvfb(Display* display) {
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display, DefaultRootWindow(display));
    if (!resources) return false;
    bool xvfb = false;
    if (resources->noutput == 1) {
      XRROutputInfo* info = XRRGetOutputInfo(display, resources, resources->outputs[0]);
      xvfb = info && std::strcmp(info->name, "screen") == 0;
      if (info) XRRFreeOutputInfo(info);
    }
    XRRFreeScreenResources(resources);
    return xvfb;
  }

} // namespace

TEST_CASE("On Xvfb the XRandR backend switches to 23.976 Hz for a film and back") {
  if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
    MESSAGE("Wayland session: nothing to switch");
    return;
  }
  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    MESSAGE("no X display: the XRandR test needs xvfb-run");
    return;
  }
  if (!onXvfb(display)) {
    MESSAGE("not Xvfb: the test does not touch a real display");
    XCloseDisplay(display);
    return;
  }
  // The added modes are 1920x1080; Xvfb refuses them (BadMatch, which ends
  // the process) on a screen of another size.
  const Screen* screen = DefaultScreenOfDisplay(display);
  if (WidthOfScreen(screen) != 1920 || HeightOfScreen(screen) != 1080) {
    MESSAGE("Xvfb screen is not 1920x1080: run it with xvfb-run -s \"-screen 0 1920x1080x24\"");
    XCloseDisplay(display);
    return;
  }
  REQUIRE(addMode(display, "jfd_23.976", 74175824, 2750));
  REQUIRE(addMode(display, "jfd_24", 74250000, 2750));
  REQUIRE(addMode(display, "jfd_60", 148500000, 2200));
  // The modes live as long as the connection that made them.
  struct Closer {
    Display* display;
    ~Closer() { XCloseDisplay(display); }
  } closer{display};

  const auto modes = jfd::platform::createSystemDisplayModes();
  REQUIRE(modes->backend() == QStringLiteral("xrandr"));
  REQUIRE(modes->screens().contains(XvfbOutput));
  // Xvfb's own mode, put back at the end for the tests that share the server.
  const auto initial = modes->currentMode(XvfbOutput);
  REQUIRE(initial.has_value());
  // Start at 60 Hz, as a desktop would.
  DisplayMode at60;
  for (const DisplayMode& mode : modes->modes(XvfbOutput))
    if (DisplayMode::sameRefreshRate(mode.refreshRate, 60) && mode.width == 1920) at60 = mode;
  REQUIRE(at60.id != 0);
  REQUIRE(modes->setMode(XvfbOutput, at60));
  REQUIRE(modes->currentMode(XvfbOutput)->refreshRate == doctest::Approx(60));

  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString journal = dir.filePath(QStringLiteral("display-mode.json"));
  DisplayModeSwitcher switcher(*modes, journal);
  bool ready = false;
  const auto choice = switcher.prepare(XvfbOutput, 23.976, [&] { ready = true; });
  CHECK(choice.kind == jfd::platform::ModeChoice::Kind::Switch);
  // The server's notice (RRNotify) ends the wait, well before the 2 s.
  REQUIRE(spinUntil([&] { return ready; }, 1500));
  CHECK(modes->currentMode(XvfbOutput)->refreshRate == doctest::Approx(24000.0 / 1001.0).epsilon(1e-5));
  CHECK(QFile::exists(journal));

  // 24 fps is not 23.976: the next video switches to 24 Hz.
  ready = false;
  switcher.prepare(XvfbOutput, 24, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }, 3000));
  CHECK(modes->currentMode(XvfbOutput)->refreshRate == doctest::Approx(24).epsilon(1e-5));

  switcher.restore();
  CHECK(modes->currentMode(XvfbOutput)->refreshRate == doctest::Approx(60));
  CHECK_FALSE(QFile::exists(journal));

  // A crashed run: the journal says 60, the display is at 23.976.
  ready = false;
  switcher.prepare(XvfbOutput, 23.976, [&] { ready = true; });
  REQUIRE(spinUntil([&] { return ready; }, 3000));
  REQUIRE(QFile::copy(journal, journal + QStringLiteral(".kept")));
  switcher.restore();
  for (const DisplayMode& mode : modes->modes(XvfbOutput))
    if (DisplayMode::sameRefreshRate(mode.refreshRate, 24000.0 / 1001.0)) REQUIRE(modes->setMode(XvfbOutput, mode));
  REQUIRE(QFile::rename(journal + QStringLiteral(".kept"), journal));
  DisplayModeSwitcher nextStart(*modes, journal);
  CHECK(nextStart.restorePending());
  CHECK(modes->currentMode(XvfbOutput)->refreshRate == doctest::Approx(60));
  CHECK(modes->setMode(XvfbOutput, *initial));
}
