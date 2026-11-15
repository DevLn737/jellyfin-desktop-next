#include <jfd/platform/DisplayModes.h>

#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QTimer>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <cstring>

// X11 through XRandR 1.2+ (a CRTC's mode, set with XRRSetCrtcConfig at the
// same size and position). On its own Xlib connection to $DISPLAY, so it
// does not need Qt's xcb connection; its change events come through a
// socket notifier on the thread that created it.
namespace jfd::platform {

  namespace {
    Q_LOGGING_CATEGORY(lcModes, "jfd.platform.display")

    // A Wayland session: XWayland's RandR is emulated and changes nothing.
    bool waylandSession() {
      if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) return true;
      return qEnvironmentVariable("XDG_SESSION_TYPE").compare(QLatin1String("wayland"), Qt::CaseInsensitive) == 0;
    }

    double rateOf(const XRRModeInfo& info) {
      if (info.hTotal == 0 || info.vTotal == 0) return 0;
      double rate = static_cast<double>(info.dotClock) / (static_cast<double>(info.hTotal) * info.vTotal);
      if (info.modeFlags & RR_DoubleScan) rate /= 2;
      if (info.modeFlags & RR_Interlace) rate *= 2;
      return rate;
    }

    DisplayMode modeOf(const XRRModeInfo& info) {
      DisplayMode mode;
      mode.width = static_cast<int>(info.width);
      mode.height = static_cast<int>(info.height);
      mode.refreshRate = rateOf(info);
      mode.interlaced = (info.modeFlags & RR_Interlace) != 0;
      mode.id = info.id;
      return mode;
    }

    const XRRModeInfo* findMode(const XRRScreenResources* resources, RRMode id) {
      for (int i = 0; i < resources->nmode; ++i)
        if (resources->modes[i].id == id) return &resources->modes[i];
      return nullptr;
    }

    struct Resources {
      Display* display;
      XRRScreenResources* resources;
      explicit Resources(Display* d)
          : display(d), resources(XRRGetScreenResourcesCurrent(d, DefaultRootWindow(d))) {}
      ~Resources() {
        if (resources) XRRFreeScreenResources(resources);
      }
      Resources(const Resources&) = delete;
      Resources& operator=(const Resources&) = delete;
    };

    struct Output {
      XRROutputInfo* info = nullptr;
      RROutput id = 0;
      Output() = default;
      Output(const Output&) = delete;
      Output& operator=(const Output&) = delete;
      ~Output() {
        if (info) XRRFreeOutputInfo(info);
      }
    };

    // The connected output named `name`, with a CRTC driving it.
    bool findOutput(Display* display, const XRRScreenResources* resources, const QString& name, Output& out) {
      const QByteArray wanted = name.toUtf8();
      for (int i = 0; i < resources->noutput; ++i) {
        XRROutputInfo* info = XRRGetOutputInfo(display, const_cast<XRRScreenResources*>(resources), resources->outputs[i]);
        if (!info) continue;
        if (info->connection == RR_Connected && info->crtc != 0 &&
            QByteArray(info->name, info->nameLen) == wanted) {
          out.info = info;
          out.id = resources->outputs[i];
          return true;
        }
        XRRFreeOutputInfo(info);
      }
      return false;
    }

    class XRandRDisplayModes final : public DisplayModes {
    public:
      XRandRDisplayModes(Display* display, int eventBase, QObject* parent)
          : DisplayModes(parent), m_display(display), m_eventBase(eventBase),
            m_notifier(ConnectionNumber(display), QSocketNotifier::Read) {
        XRRSelectInput(m_display, DefaultRootWindow(m_display), RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask);
        XFlush(m_display);
        QObject::connect(&m_notifier, &QSocketNotifier::activated, this, [this] { readEvents(); });
      }

      ~XRandRDisplayModes() override {
        m_notifier.setEnabled(false);
        XCloseDisplay(m_display);
      }

      QString backend() const override { return QStringLiteral("xrandr"); }
      bool isSupported() const override { return true; }

      QStringList screens() const override {
        QStringList names;
        Resources current(m_display);
        if (!current.resources) return names;
        for (int i = 0; i < current.resources->noutput; ++i) {
          XRROutputInfo* info = XRRGetOutputInfo(m_display, current.resources, current.resources->outputs[i]);
          if (!info) continue;
          if (info->connection == RR_Connected && info->crtc != 0)
            names.append(QString::fromUtf8(info->name, info->nameLen));
          XRRFreeOutputInfo(info);
        }
        return names;
      }

      QList<DisplayMode> modes(const QString& screen) const override {
        QList<DisplayMode> list;
        Resources current(m_display);
        Output output;
        if (!current.resources || !findOutput(m_display, current.resources, screen, output)) return list;
        for (int i = 0; i < output.info->nmode; ++i)
          if (const XRRModeInfo* info = findMode(current.resources, output.info->modes[i])) list.append(modeOf(*info));
        return list;
      }

      std::optional<DisplayMode> currentMode(const QString& screen) const override {
        Resources current(m_display);
        Output output;
        if (!current.resources || !findOutput(m_display, current.resources, screen, output)) return std::nullopt;
        XRRCrtcInfo* crtc = XRRGetCrtcInfo(m_display, current.resources, output.info->crtc);
        if (!crtc) return std::nullopt;
        std::optional<DisplayMode> mode;
        if (const XRRModeInfo* info = findMode(current.resources, crtc->mode)) mode = modeOf(*info);
        XRRFreeCrtcInfo(crtc);
        drainSoon();
        return mode;
      }

      bool setMode(const QString& screen, const DisplayMode& mode) override {
        Resources current(m_display);
        Output output;
        if (!current.resources || !findOutput(m_display, current.resources, screen, output)) return false;
        bool listed = false;
        for (int i = 0; i < output.info->nmode; ++i) listed = listed || output.info->modes[i] == mode.id;
        if (!listed || mode.id == 0) {
          qCWarning(lcModes) << "mode" << mode.toString() << "is not one of" << screen << "'s";
          return false;
        }
        XRRCrtcInfo* crtc = XRRGetCrtcInfo(m_display, current.resources, output.info->crtc);
        if (!crtc) return false;
        // The same size, position, rotation and outputs: only the timing
        // changes, so the screen's size stays as it is.
        const Status status =
            XRRSetCrtcConfig(m_display, current.resources, output.info->crtc, CurrentTime, crtc->x, crtc->y,
                             static_cast<RRMode>(mode.id), crtc->rotation, crtc->outputs, crtc->noutput);
        XRRFreeCrtcInfo(crtc);
        XSync(m_display, False);
        drainSoon();
        if (status != RRSetConfigSuccess) {
          qCWarning(lcModes) << "XRandR refused" << mode.toString() << "on" << screen << "status" << status;
          return false;
        }
        return true;
      }

    private:
      // A round trip of our own reads the server's events into Xlib's
      // queue, where the socket notifier does not see them.
      void drainSoon() const {
        if (m_drainPending || XQLength(m_display) == 0) return;
        m_drainPending = true;
        QTimer::singleShot(0, const_cast<XRandRDisplayModes*>(this), [this] {
          m_drainPending = false;
          const_cast<XRandRDisplayModes*>(this)->readEvents();
        });
      }

      void readEvents() {
        bool changed = false;
        while (XPending(m_display) > 0) {
          XEvent event;
          XNextEvent(m_display, &event);
          XRRUpdateConfiguration(&event);
          if (event.type == m_eventBase + RRScreenChangeNotify || event.type == m_eventBase + RRNotify)
            changed = true;
        }
        if (changed) Q_EMIT modeChanged();
      }

      Display* m_display;
      int m_eventBase;
      QSocketNotifier m_notifier;
      mutable bool m_drainPending = false;
    };
  } // namespace

  bool displayModesAvailable() { return !waylandSession() && !qEnvironmentVariableIsEmpty("DISPLAY"); }

  std::unique_ptr<DisplayModes> createSystemDisplayModes(QObject* parent) {
    if (waylandSession()) {
      qCInfo(lcModes) << "Wayland session: the display mode is not changed (XWayland's RandR is emulated)";
      return createUnsupportedDisplayModes(parent);
    }
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
      qCInfo(lcModes) << "no X display: the display mode is not changed";
      return createUnsupportedDisplayModes(parent);
    }
    int eventBase = 0;
    int errorBase = 0;
    int major = 0;
    int minor = 0;
    if (!XRRQueryExtension(display, &eventBase, &errorBase) || !XRRQueryVersion(display, &major, &minor) ||
        major < 1 || (major == 1 && minor < 2)) {
      qCInfo(lcModes) << "no XRandR 1.2 on this X server: the display mode is not changed";
      XCloseDisplay(display);
      return createUnsupportedDisplayModes(parent);
    }
    // XWayland under a session that does not say so.
    {
      Resources current(display);
      for (int i = 0; current.resources && i < current.resources->noutput; ++i) {
        XRROutputInfo* info = XRRGetOutputInfo(display, current.resources, current.resources->outputs[i]);
        if (!info) continue;
        const bool xwayland = std::strncmp(info->name, "XWAYLAND", 8) == 0;
        XRRFreeOutputInfo(info);
        if (xwayland) {
          qCInfo(lcModes) << "XWayland: the display mode is not changed";
          XCloseDisplay(display);
          return createUnsupportedDisplayModes(parent);
        }
      }
    }
    return std::make_unique<XRandRDisplayModes>(display, eventBase, parent);
  }

} // namespace jfd::platform
