#pragma once

#include <QObject>
#include <QPointer>
#include <qqmlintegration.h>

class QQmlEngine;
class QJSEngine;

// Where the last input came from: a gamepad or the phone's remote (the keys
// InputRouter sends), or a keyboard, mouse or touch screen (events from the
// system). The shell copies `gamepad` to Theme.gamepadInput, and with TV
// mode it makes the interface focus-driven (Theme.focusDriven): the
// player's arrows move between its controls and Select acts, the on-screen
// keyboard offers itself in text fields. TV mode's sizes do not change.
//
// One per application. InputRouter marks every action it routes; a real
// key press, mouse button, wheel or touch on a window clears the mark. A
// mouse that only moves does not (a nudged mouse beside a gamepad).
namespace jfd::ui {

  class InputMode final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool gamepad READ gamepad NOTIFY gamepadChanged)

  public:
    // Created on first use, owned by the application object.
    static InputMode* instance();
    static InputMode* create(QQmlEngine*, QJSEngine*);

    bool gamepad() const { return m_gamepad; }
    // A gamepad (or the remote) was used.
    void noteGamepad() { setGamepad(true); }
    // Only for tests and a fresh start.
    void setGamepad(bool gamepad);

  Q_SIGNALS:
    void gamepadChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    explicit InputMode(QObject* parent);

    bool m_gamepad = false;
    inline static QPointer<InputMode> s_instance;
  };

} // namespace jfd::ui
