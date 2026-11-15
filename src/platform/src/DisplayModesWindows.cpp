#include <jfd/platform/DisplayModes.h>

#include <QLoggingCategory>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <vector>

// Windows: the modes come from EnumDisplaySettingsExW, which gives whole
// rates and names the NTSC ones one lower (23 for 23.976, 59 for 59.94).
// The switch goes through SetDisplayConfig with the rate as a rational
// (24000/1001), so fractional rates are exact; when that is refused,
// ChangeDisplaySettingsExW with CDS_FULLSCREEN (whole rates only; the system
// would also put the mode back if the process ends). SetDisplayConfig is not
// undone by the system: the journal (DisplayModeSwitcher) puts the mode back
// on the next start after a crash.
namespace jfd::platform {

  namespace {
    Q_LOGGING_CATEGORY(lcModes, "jfd.platform.display")

    // Whole rates Windows lists one lower for their NTSC twin (x 1000/1001).
    bool ntscBelow(DWORD rate) {
      switch (rate + 1) {
        case 24: case 30: case 48: case 60: case 72: case 96: case 120: case 144: case 240:
          return true;
        default:
          return false;
      }
    }

    double rateOf(DWORD listed) {
      if (ntscBelow(listed)) return static_cast<double>(listed + 1) * 1000.0 / 1001.0;
      return static_cast<double>(listed);
    }

    // The rate as SetDisplayConfig takes it.
    DISPLAYCONFIG_RATIONAL rationalOf(double rate) {
      const double whole = std::round(rate * 1.001);
      if (std::abs(rate - whole * 1000.0 / 1001.0) < 0.0005 * rate && std::abs(rate - std::round(rate)) > 0.01)
        return {static_cast<UINT32>(whole * 1000), 1001};
      return {static_cast<UINT32>(std::lround(rate * 1000)), 1000};
    }

    DWORD listedRate(double rate) {
      // 23.976 is listed as 23, 24 as 24.
      return static_cast<DWORD>(std::floor(rate + 0.01));
    }

    std::wstring wide(const QString& text) { return text.toStdWString(); }

    struct Config {
      std::vector<DISPLAYCONFIG_PATH_INFO> paths;
      std::vector<DISPLAYCONFIG_MODE_INFO> modes;

      bool query() {
        for (int attempt = 0; attempt < 3; ++attempt) {
          UINT32 pathCount = 0;
          UINT32 modeCount = 0;
          if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return false;
          paths.resize(pathCount);
          modes.resize(modeCount);
          const LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                                                 modes.data(), nullptr);
          if (result == ERROR_INSUFFICIENT_BUFFER) continue;
          if (result != ERROR_SUCCESS) return false;
          paths.resize(pathCount);
          modes.resize(modeCount);
          return true;
        }
        return false;
      }
    };

    QString gdiName(const DISPLAYCONFIG_PATH_INFO& path) {
      DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
      source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source.header.size = sizeof(source);
      source.header.adapterId = path.sourceInfo.adapterId;
      source.header.id = path.sourceInfo.id;
      if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) return {};
      return QString::fromWCharArray(source.viewGdiDeviceName);
    }

    QString monitorName(const DISPLAYCONFIG_PATH_INFO& path) {
      DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
      target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
      target.header.size = sizeof(target);
      target.header.adapterId = path.targetInfo.adapterId;
      target.header.id = path.targetInfo.id;
      if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) return {};
      return QString::fromWCharArray(target.monitorFriendlyDeviceName);
    }

    class WindowsDisplayModes final : public DisplayModes {
    public:
      using DisplayModes::DisplayModes;

      QString backend() const override { return QStringLiteral("windows"); }
      bool isSupported() const override { return true; }

      QStringList screens() const override {
        QStringList names;
        Config config;
        if (!config.query()) return names;
        for (const DISPLAYCONFIG_PATH_INFO& path : config.paths) {
          const QString name = gdiName(path);
          if (!name.isEmpty() && !names.contains(name)) names.append(name);
        }
        return names;
      }

      QList<DisplayMode> modes(const QString& screen) const override {
        QList<DisplayMode> list;
        const QString device = deviceName(screen);
        if (device.isEmpty()) return list;
        const std::wstring name = wide(device);
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        for (DWORD i = 0; EnumDisplaySettingsExW(name.c_str(), i, &mode, 0); ++i) {
          // 32-bit colour only; the others repeat the same timings.
          if (mode.dmBitsPerPel != 32) continue;
          const DisplayMode entry = modeOf(mode);
          if (!list.contains(entry)) list.append(entry);
        }
        return list;
      }

      std::optional<DisplayMode> currentMode(const QString& screen) const override {
        const QString device = deviceName(screen);
        if (device.isEmpty()) return std::nullopt;
        const std::wstring name = wide(device);
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(name.c_str(), ENUM_CURRENT_SETTINGS, &mode, 0)) return std::nullopt;
        return modeOf(mode);
      }

      bool setMode(const QString& screen, const DisplayMode& mode) override {
        const QString device = deviceName(screen);
        if (device.isEmpty()) return false;
        bool done = setByDisplayConfig(device, mode);
        if (!done) done = setByDevMode(device, mode);
        if (done) {
          // SetDisplayConfig returns once the mode is set; the event loop
          // hears about it after this call, as with a system notification.
          QTimer::singleShot(0, this, [this] { Q_EMIT modeChanged(); });
        }
        return done;
      }

    private:
      static DisplayMode modeOf(const DEVMODEW& mode) {
        DisplayMode entry;
        entry.width = static_cast<int>(mode.dmPelsWidth);
        entry.height = static_cast<int>(mode.dmPelsHeight);
        entry.refreshRate = rateOf(mode.dmDisplayFrequency);
        entry.interlaced = (mode.dmDisplayFlags & DM_INTERLACED) != 0;
        return entry;
      }

      // "\\.\DISPLAY1", from that name or from the monitor's own name when
      // only one monitor carries it.
      QString deviceName(const QString& screen) const {
        Config config;
        if (!config.query()) return {};
        QString byMonitor;
        int monitorMatches = 0;
        for (const DISPLAYCONFIG_PATH_INFO& path : config.paths) {
          const QString gdi = gdiName(path);
          if (gdi.compare(screen, Qt::CaseInsensitive) == 0) return gdi;
          if (!screen.isEmpty() && monitorName(path) == screen) {
            byMonitor = gdi;
            ++monitorMatches;
          }
        }
        return monitorMatches == 1 ? byMonitor : QString();
      }

      bool setByDisplayConfig(const QString& device, const DisplayMode& mode) {
        Config config;
        if (!config.query()) return false;
        bool found = false;
        for (DISPLAYCONFIG_PATH_INFO& path : config.paths) {
          if (gdiName(path).compare(device, Qt::CaseInsensitive) != 0) continue;
          // The source mode keeps the size; without a target mode the
          // system picks the timing for targetInfo.refreshRate.
          if (path.sourceInfo.modeInfoIdx != DISPLAYCONFIG_PATH_MODE_IDX_INVALID &&
              path.sourceInfo.modeInfoIdx < config.modes.size()) {
            DISPLAYCONFIG_SOURCE_MODE& source = config.modes[path.sourceInfo.modeInfoIdx].sourceMode;
            if (static_cast<int>(source.width) != mode.width || static_cast<int>(source.height) != mode.height)
              return false;
          }
          path.targetInfo.refreshRate = rationalOf(mode.refreshRate);
          path.targetInfo.modeInfoIdx = DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
          found = true;
        }
        if (!found) return false;
        const LONG result =
            SetDisplayConfig(static_cast<UINT32>(config.paths.size()), config.paths.data(),
                             static_cast<UINT32>(config.modes.size()), config.modes.data(),
                             SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES);
        if (result != ERROR_SUCCESS) {
          qCInfo(lcModes) << "SetDisplayConfig refused" << mode.toString() << "on" << device << "error" << result;
          return false;
        }
        return true;
      }

      bool setByDevMode(const QString& device, const DisplayMode& mode) {
        const std::wstring name = wide(device);
        DEVMODEW devMode = {};
        devMode.dmSize = sizeof(devMode);
        if (!EnumDisplaySettingsExW(name.c_str(), ENUM_CURRENT_SETTINGS, &devMode, 0)) return false;
        devMode.dmPelsWidth = static_cast<DWORD>(mode.width);
        devMode.dmPelsHeight = static_cast<DWORD>(mode.height);
        devMode.dmDisplayFrequency = listedRate(mode.refreshRate);
        devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        const LONG result = ChangeDisplaySettingsExW(name.c_str(), &devMode, nullptr, CDS_FULLSCREEN, nullptr);
        if (result != DISP_CHANGE_SUCCESSFUL) {
          qCWarning(lcModes) << "ChangeDisplaySettingsExW refused" << mode.toString() << "on" << device << "error"
                             << result;
          return false;
        }
        return true;
      }
    };
  } // namespace

  bool displayModesAvailable() { return true; }

  std::unique_ptr<DisplayModes> createSystemDisplayModes(QObject* parent) {
    return std::make_unique<WindowsDisplayModes>(parent);
  }

} // namespace jfd::platform
