#include "InputMode.h"

#include <QCoreApplication>
#include <QEvent>
#include <QJSEngine>

namespace jfd::ui {

  InputMode::InputMode(QObject* parent) : QObject(parent) {
    if (parent) parent->installEventFilter(this);
  }

  InputMode* InputMode::instance() {
    if (!s_instance) s_instance = new InputMode(QCoreApplication::instance());
    return s_instance;
  }

  InputMode* InputMode::create(QQmlEngine*, QJSEngine*) {
    InputMode* mode = instance();
    QJSEngine::setObjectOwnership(mode, QJSEngine::CppOwnership);
    return mode;
  }

  void InputMode::setGamepad(bool gamepad) {
    if (m_gamepad == gamepad) return;
    m_gamepad = gamepad;
    Q_EMIT gamepadChanged();
  }

  bool InputMode::eventFilter(QObject* watched, QEvent* event) {
    // The system's events reach the window first (spontaneous); the same
    // event passed on to its items no longer counts as spontaneous.
    if (!m_gamepad || !event->spontaneous() || !watched->isWindowType()) return false;
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::TouchBegin: setGamepad(false); break;
    default: break;
    }
    return false;
  }

} // namespace jfd::ui
