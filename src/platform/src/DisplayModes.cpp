#include <jfd/platform/DisplayModes.h>

#include <algorithm>
#include <cmath>

namespace jfd::platform {

  namespace {
    bool within(double value, double target, double tolerance) {
      return std::abs(value - target) <= tolerance * target;
    }

    // The whole number of display refreshes per video frame, or 0 when the
    // display rate is not a whole multiple of the frame rate.
    int multipleOf(double displayRate, double frameRate) {
      const double ratio = displayRate / frameRate;
      const double whole = std::round(ratio);
      if (whole < 1 || whole > 1000) return 0;
      return within(displayRate, whole * frameRate, RefreshTolerance) ? static_cast<int>(whole) : 0;
    }
  } // namespace

  bool DisplayMode::sameRefreshRate(double a, double b) {
    if (a <= 0 || b <= 0) return a == b;
    return std::abs(a - b) <= 0.0001 * std::max(a, b);
  }

  QString DisplayMode::toString() const {
    return QStringLiteral("%1x%2%3 @ %4 Hz")
        .arg(width)
        .arg(height)
        .arg(interlaced ? QStringLiteral("i") : QString())
        .arg(refreshRate, 0, 'f', 3);
  }

  ModeChoice chooseDisplayMode(double frameRate, const DisplayMode& current, const QList<DisplayMode>& modes) {
    ModeChoice choice;
    if (!(frameRate > 0 && frameRate <= 300) || current.refreshRate <= 0) return choice;

    if (!current.interlaced) {
      if (const int multiple = multipleOf(current.refreshRate, frameRate)) {
        choice.kind = ModeChoice::Kind::Keep;
        choice.mode = current;
        choice.multiple = multiple;
        return choice;
      }
    }

    const DisplayMode* exact = nullptr;
    const DisplayMode* highMultiple = nullptr; // the smallest at 100 Hz or more
    const DisplayMode* lowMultiple = nullptr;  // the largest below 100 Hz
    int highFactor = 0;
    int lowFactor = 0;
    for (const DisplayMode& mode : modes) {
      if (mode.interlaced || mode.width != current.width || mode.height != current.height || mode.refreshRate <= 0)
        continue;
      const int multiple = multipleOf(mode.refreshRate, frameRate);
      if (multiple == 1) {
        // Of two listings of the same rate the first one wins.
        if (exact == nullptr) exact = &mode;
      } else if (multiple > 1) {
        if (mode.refreshRate >= 100) {
          if (highMultiple == nullptr || mode.refreshRate < highMultiple->refreshRate) {
            highMultiple = &mode;
            highFactor = multiple;
          }
        } else if (lowMultiple == nullptr || mode.refreshRate > lowMultiple->refreshRate) {
          lowMultiple = &mode;
          lowFactor = multiple;
        }
      }
    }

    if (exact != nullptr) {
      choice.kind = ModeChoice::Kind::Switch;
      choice.mode = *exact;
      choice.multiple = 1;
    } else if (highMultiple != nullptr) {
      choice.kind = ModeChoice::Kind::Switch;
      choice.mode = *highMultiple;
      choice.multiple = highFactor;
    } else if (lowMultiple != nullptr) {
      choice.kind = ModeChoice::Kind::Switch;
      choice.mode = *lowMultiple;
      choice.multiple = lowFactor;
    }
    return choice;
  }

  namespace {
    class UnsupportedDisplayModes final : public DisplayModes {
    public:
      using DisplayModes::DisplayModes;
      QString backend() const override { return QStringLiteral("none"); }
      bool isSupported() const override { return false; }
      QStringList screens() const override { return {}; }
      QList<DisplayMode> modes(const QString&) const override { return {}; }
      std::optional<DisplayMode> currentMode(const QString&) const override { return std::nullopt; }
      bool setMode(const QString&, const DisplayMode&) override { return false; }
    };
  } // namespace

  std::unique_ptr<DisplayModes> createUnsupportedDisplayModes(QObject* parent) {
    return std::make_unique<UnsupportedDisplayModes>(parent);
  }

  bool frameRatesDisagree(double serverRate, double containerRate) {
    if (!(serverRate > 0) || !(containerRate > 0)) return false;
    return std::abs(serverRate - containerRate) > 0.01 * serverRate;
  }

} // namespace jfd::platform
