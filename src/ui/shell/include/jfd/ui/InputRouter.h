#pragma once

#include <jfd/platform/InputAction.h>

#include <QObject>
#include <QPointer>
#include <QWindow>

#include <functional>
#include <optional>

// Delivers InputAction (a gamepad, later the server's navigation commands)
// to the interface. Navigation actions become key presses sent to the window
// itself (QCoreApplication::sendEvent), not to its focused item: that way
// Shortcut, ShortcutOverride, popups and "nothing has focus" behave as with a
// keyboard. Repeats carry autoRepeat. Back is Escape, which the shell, the
// player and the popups all understand (AppShell.qml leaves on Escape,
// Backspace and Alt+Left; only the player knows Key_Back). Search opens the
// Ctrl+K palette. The media actions are not keys: they go to the media
// handler (MediaSessionBridge), the path of the system media controls, so
// they work on every screen and in a SyncPlay group.
namespace jfd::ui {

  class InputRouter final : public QObject {
    Q_OBJECT
  public:
    using MediaHandler = std::function<void(platform::InputAction action)>;

    struct KeyPress {
      Qt::Key key = Qt::Key_unknown;
      Qt::KeyboardModifiers modifiers = Qt::NoModifier;
      bool operator==(const KeyPress&) const = default;
    };

    explicit InputRouter(QObject* parent = nullptr);

    void setWindow(QWindow* window);
    QWindow* window() const { return m_window; }
    // PlayPause, SeekForward and SeekBack; without a handler they are dropped.
    void setMediaHandler(MediaHandler handler);

    // The key a navigation action sends; nullopt for the media actions.
    static std::optional<KeyPress> keyFor(platform::InputAction action);

  public Q_SLOTS:
    void route(jfd::platform::InputAction action, bool repeat);

  private:
    QPointer<QWindow> m_window;
    MediaHandler m_media;
  };

} // namespace jfd::ui
