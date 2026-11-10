#pragma once

#include <jfd/platform/InputAction.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

// Gamepads through SDL3 (gamepad subsystem only): Xbox, PlayStation,
// Nintendo Switch and 8BitDo controllers in XInput, DirectInput and Switch
// modes, by SDL's built-in mapping database (and SDL_GAMECONTROLLERCONFIG
// for others). Turns presses into InputAction (GamepadMapper's layout and
// repeat).
//
// SDL is started and polled on the thread that creates the object (the GUI
// thread): SDL expects its events to be pumped where it was initialised,
// which matters for Windows' RawInput and Windows.Gaming.Input. The timer
// polls every 8 ms while a gamepad is connected and every 250 ms otherwise,
// only to notice one being plugged in. SDL has no window of its own, so it
// is told to deliver events in the background; whether the application is
// active is decided by the owner (setAcceptsInput).
namespace jfd::platform {

  class GamepadInput : public QObject {
    Q_OBJECT
  public:
    static constexpr int ActivePollMs = 8;
    static constexpr int IdlePollMs = 250;

    explicit GamepadInput(QObject* parent = nullptr);
    ~GamepadInput() override;

    // False when the build has no SDL (JFD_GAMEPAD off).
    static bool isSupported();
    // The SDL version linked in, empty without SDL.
    static QString sdlVersion();

    // Starts SDL's gamepad subsystem and the polling; gamepads connected
    // already are reported at once. False (and a warning in the log) when
    // SDL could not start.
    bool start();
    // Closes the gamepads and SDL's subsystem; held actions stop.
    void stop();
    bool isRunning() const;

    // False while the window is not active (unless the user allows input in
    // the background): presses are dropped and held directions stop
    // repeating. Connecting and disconnecting is still followed.
    void setAcceptsInput(bool accepts);
    bool acceptsInput() const;

    int connectedCount() const;
    // Names as SDL reports them ("Xbox Series X Controller"); no serial
    // numbers.
    QStringList controllerNames() const;
    // The current poll interval: ActivePollMs or IdlePollMs.
    int pollInterval() const;

    // Handles SDL's pending events now; the timer calls it.
    void poll();

  Q_SIGNALS:
    void actionTriggered(jfd::platform::InputAction action, bool repeat);
    // A gamepad was connected or disconnected.
    void controllersChanged();

  private:
    struct Private;
    std::unique_ptr<Private> d;
  };

} // namespace jfd::platform
