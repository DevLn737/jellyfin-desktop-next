#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <optional>

// The display modes of a screen: list them, read the current one and switch
// the refresh rate, so a 23.976 or 25 fps video plays on a display running
// at a rate it divides (docs/display-refresh-rate.md).
//
// Backends: Windows (QueryDisplayConfig / SetDisplayConfig, which take the
// fractional rates 23.976, 29.97, 59.94 and 119.88 as rationals; falling back
// to ChangeDisplaySettingsExW with CDS_FULLSCREEN, whole rates only) and X11
// through XRandR. Wayland has no protocol for a client to change the mode,
// and XWayland's XRandR is an emulation that changes nothing, so a Wayland
// session (and any other system) gets the backend that is not supported.
//
// A screen is named as Qt names it: QScreen::name() on X11 (the RandR
// output, "DP-1"), the GDI device name ("\\.\DISPLAY1") on Windows, where
// the monitor's own name is accepted too when no two monitors share it.
namespace jfd::platform {

  struct DisplayMode {
    int width = 0;
    int height = 0;
    // In hertz, as exact as the system reports it: 23.976023... for
    // 24000/1001.
    double refreshRate = 0;
    bool interlaced = false;
    // The backend's own handle (an XRandR mode id); 0 on Windows, where the
    // size and rate say which mode it is.
    std::uint64_t id = 0;

    friend bool operator==(const DisplayMode& a, const DisplayMode& b) {
      return a.width == b.width && a.height == b.height && a.interlaced == b.interlaced &&
             sameRefreshRate(a.refreshRate, b.refreshRate);
    }
    // Rates the same display reports twice may differ in the last digits
    // (59.94 and 59.9401): within 0.01 %.
    static bool sameRefreshRate(double a, double b);
    // "1920x1080 @ 23.976 Hz", for the log.
    QString toString() const;
  };

  // What chooseDisplayMode decided.
  struct ModeChoice {
    enum class Kind {
      // The current mode already fits the video (the same rate or a whole
      // multiple of it, 24 fps on 120 or 144 Hz): nothing to change.
      Keep,
      // Switch to `mode`.
      Switch,
      // No mode fits; leave the display as it is.
      NoMatch,
    };
    Kind kind = Kind::NoMatch;
    DisplayMode mode;
    // How many display refreshes show one video frame (1 for the same
    // rate); 0 with NoMatch.
    int multiple = 0;
  };

  // How close a display rate must be to the video's (or to a whole multiple
  // of it) to count as a match: 0.05 %, half the 0.1 % between 24 and
  // 23.976 Hz, so those two never match each other.
  inline constexpr double RefreshTolerance = 0.0005;

  // The mode for a video at `frameRate` frames per second, among `modes` of
  // the screen now running `current`. Only modes with the current size, not
  // interlaced, are considered. In order:
  //   1. the current mode, if its rate is the video's or a whole multiple of
  //      it: Keep;
  //   2. a mode at the video's own rate (23.976 on 23.976 Hz, never on 24);
  //   3. a whole multiple of the rate: the smallest one of 100 Hz or more
  //      (24 -> 120 rather than 144), else the largest below 100 (25 -> 50);
  //   4. otherwise NoMatch. 23.976 on {60, 120, 144, 165} is NoMatch:
  //      23.976 x 5 = 119.88, 0.1 % away from 120.
  // A frame rate that is not a number above 0 and at most 300 is NoMatch.
  ModeChoice chooseDisplayMode(double frameRate, const DisplayMode& current, const QList<DisplayMode>& modes);

  // The frame rate the server reported (RealFrameRate) and the one mpv
  // found in the file (container-fps) differ by more than 1 %: interlaced or
  // variable frame rate files. Not when either is unknown (0 or less).
  bool frameRatesDisagree(double serverRate, double containerRate);

  class DisplayModes : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    ~DisplayModes() override = default;

    // "windows", "xrandr" or "none", for the log and the diagnostic report.
    virtual QString backend() const = 0;
    // False where the mode cannot be changed (Wayland, XWayland, no RandR,
    // other systems); then every list below is empty.
    virtual bool isSupported() const = 0;
    // The connected screens, by the names the methods below take.
    virtual QStringList screens() const = 0;
    virtual QList<DisplayMode> modes(const QString& screen) const = 0;
    virtual std::optional<DisplayMode> currentMode(const QString& screen) const = 0;
    // Asks the system for `mode` (one of modes(screen)). False when the
    // system refused; the change itself is announced by modeChanged.
    virtual bool setMode(const QString& screen, const DisplayMode& mode) = 0;

  Q_SIGNALS:
    // The system reports that a screen's mode changed (any screen: compare
    // currentMode to see which).
    void modeChanged();
  };

  // The backend for this platform and session; one that is not supported
  // where there is none.
  std::unique_ptr<DisplayModes> createSystemDisplayModes(QObject* parent = nullptr);
  // Whether this build and session can switch modes at all (Windows; X11
  // outside a Wayland session with $DISPLAY set), without connecting to
  // anything: for hiding the setting before the application starts.
  bool displayModesAvailable();
  // The backend that is not supported: no screens, no modes, every switch
  // refused.
  std::unique_ptr<DisplayModes> createUnsupportedDisplayModes(QObject* parent = nullptr);

} // namespace jfd::platform
