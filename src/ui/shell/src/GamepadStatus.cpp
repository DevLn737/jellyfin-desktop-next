#include "GamepadStatus.h"

#include <QCoreApplication>

namespace jfd::ui {

  using platform::InputAction;

  GamepadStatus::GamepadStatus(QObject* parent) : QObject(parent) {}

  void GamepadStatus::setAvailable(bool available) {
    if (m_available == available) return;
    m_available = available;
    if (!available) stopCheck();
    Q_EMIT controllersChanged();
  }

  void GamepadStatus::setControllers(const QStringList& names) {
    if (m_names == names) return;
    m_names = names;
    Q_EMIT controllersChanged();
  }

  bool GamepadStatus::takePress(InputAction action, bool repeat) {
    if (!m_checking) return false;
    if (repeat) return true;
    const bool back = action == InputAction::Back;
    m_lastAction = actionText(action);
    ++m_pressCount;
    Q_EMIT pressed();
    if (back && m_lastWasBack) {
      stopCheck();
      return true;
    }
    m_lastWasBack = back;
    return true;
  }

  void GamepadStatus::startCheck() {
    m_lastAction.clear();
    m_pressCount = 0;
    m_lastWasBack = false;
    Q_EMIT pressed();
    if (m_checking) return;
    m_checking = true;
    Q_EMIT checkingChanged();
  }

  void GamepadStatus::stopCheck() {
    if (!m_checking) return;
    m_checking = false;
    m_lastWasBack = false;
    Q_EMIT checkingChanged();
  }

  QString GamepadStatus::actionText(InputAction action) {
    const char* text = "";
    switch (action) {
    case InputAction::Up: text = QT_TRANSLATE_NOOP("GamepadStatus", "Up"); break;
    case InputAction::Down: text = QT_TRANSLATE_NOOP("GamepadStatus", "Down"); break;
    case InputAction::Left: text = QT_TRANSLATE_NOOP("GamepadStatus", "Left"); break;
    case InputAction::Right: text = QT_TRANSLATE_NOOP("GamepadStatus", "Right"); break;
    case InputAction::Select: text = QT_TRANSLATE_NOOP("GamepadStatus", "Select"); break;
    case InputAction::Back: text = QT_TRANSLATE_NOOP("GamepadStatus", "Back"); break;
    case InputAction::Menu: text = QT_TRANSLATE_NOOP("GamepadStatus", "Menu"); break;
    case InputAction::PlayPause: text = QT_TRANSLATE_NOOP("GamepadStatus", "Play or pause"); break;
    case InputAction::SeekForward: text = QT_TRANSLATE_NOOP("GamepadStatus", "Seek forward"); break;
    case InputAction::SeekBack: text = QT_TRANSLATE_NOOP("GamepadStatus", "Seek back"); break;
    case InputAction::PageLeft: text = QT_TRANSLATE_NOOP("GamepadStatus", "Page up"); break;
    case InputAction::PageRight: text = QT_TRANSLATE_NOOP("GamepadStatus", "Page down"); break;
    case InputAction::Search: text = QT_TRANSLATE_NOOP("GamepadStatus", "Search"); break;
    }
    return QCoreApplication::translate("GamepadStatus", text);
  }

} // namespace jfd::ui
