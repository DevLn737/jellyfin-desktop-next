#include "InputRouter.h"

#include <QCoreApplication>
#include <QKeyEvent>

namespace jfd::ui {

  using platform::InputAction;

  namespace {
    // The text a keyboard gives with the key, for controls that read it.
    QString keyText(Qt::Key key) {
      switch (key) {
      case Qt::Key_Return: return QStringLiteral("\r");
      case Qt::Key_Escape: return QString(QChar(0x1b));
      default: return {};
      }
    }
  } // namespace

  InputRouter::InputRouter(QObject* parent) : QObject(parent) {}

  void InputRouter::setWindow(QWindow* window) { m_window = window; }

  void InputRouter::setMediaHandler(MediaHandler handler) { m_media = std::move(handler); }

  std::optional<InputRouter::KeyPress> InputRouter::keyFor(InputAction action) {
    switch (action) {
    case InputAction::Up: return KeyPress{Qt::Key_Up, Qt::NoModifier};
    case InputAction::Down: return KeyPress{Qt::Key_Down, Qt::NoModifier};
    case InputAction::Left: return KeyPress{Qt::Key_Left, Qt::NoModifier};
    case InputAction::Right: return KeyPress{Qt::Key_Right, Qt::NoModifier};
    case InputAction::Select: return KeyPress{Qt::Key_Return, Qt::NoModifier};
    case InputAction::Back: return KeyPress{Qt::Key_Escape, Qt::NoModifier};
    case InputAction::Menu: return KeyPress{Qt::Key_Menu, Qt::NoModifier};
    case InputAction::PageLeft: return KeyPress{Qt::Key_PageUp, Qt::NoModifier};
    case InputAction::PageRight: return KeyPress{Qt::Key_PageDown, Qt::NoModifier};
    case InputAction::Search: return KeyPress{Qt::Key_K, Qt::ControlModifier};
    case InputAction::PlayPause:
    case InputAction::SeekForward:
    case InputAction::SeekBack: return std::nullopt;
    }
    return std::nullopt;
  }

  void InputRouter::route(InputAction action, bool repeat) {
    if (platform::isMediaAction(action)) {
      if (m_media) m_media(action);
      return;
    }
    const auto key = keyFor(action);
    if (!key || !m_window) return;
    const QString text = keyText(key->key);
    // Press and release, as a key tapped (or repeated) on a keyboard. The
    // window may close in between.
    QPointer<QWindow> window = m_window;
    QKeyEvent press(QEvent::KeyPress, key->key, key->modifiers, text, repeat);
    QCoreApplication::sendEvent(window, &press);
    if (!window) return;
    QKeyEvent release(QEvent::KeyRelease, key->key, key->modifiers, text, repeat);
    QCoreApplication::sendEvent(window, &release);
  }

} // namespace jfd::ui
