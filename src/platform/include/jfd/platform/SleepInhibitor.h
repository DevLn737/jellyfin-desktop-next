#pragma once

#include <QObject>
#include <QString>

#include <memory>

// Keeps the display on and the system awake while video plays. On Windows
// through SetThreadExecutionState; on Linux through the desktop portal
// (org.freedesktop.portal.Inhibit), falling back to org.freedesktop.ScreenSaver
// when no portal answers. Calls are cheap and idempotent; the session turns it
// on while playing and off on pause, stop and exit.
namespace jfd::platform {

  class SleepInhibitor : public QObject {
    Q_OBJECT
  public:
    enum class Backend { None, WindowsExecutionState, Portal, ScreenSaver };

    using QObject::QObject;
    ~SleepInhibitor() override = default;

    // reason is shown by desktops that list inhibitors (for example "Playing video").
    virtual void setInhibited(bool inhibited, const QString& reason) = 0;
    bool isInhibited() const { return m_active; }
    // The backend holding the current inhibition, None while released.
    Backend backend() const { return m_backend; }

  Q_SIGNALS:
    // The system accepted or released the inhibition (asynchronous on Linux).
    void activeChanged(bool active);

  protected:
    void setActive(bool active, Backend backend);

  private:
    bool m_active = false;
    Backend m_backend = Backend::None;
  };

  // The inhibitor for this platform; one that does nothing where there is none.
  std::unique_ptr<SleepInhibitor> createSystemSleepInhibitor(QObject* parent = nullptr);

} // namespace jfd::platform
