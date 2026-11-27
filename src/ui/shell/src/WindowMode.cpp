#include "WindowMode.h"

#include "WindowPlacement.h"

#include <QWindow>

namespace jfd::ui {

  bool WindowMode::isFullScreen(QWindow* window) {
    return window != nullptr && window->windowStates().testFlag(Qt::WindowFullScreen);
  }

  void WindowMode::toggleFullScreen(QWindow* window) {
    if (window == nullptr) return;
    window->setWindowStates(fullScreenToggled(window->windowStates(), !isFullScreen(window)));
  }

  void WindowMode::enterFullScreen(QWindow* window) {
    if (window == nullptr || isFullScreen(window)) return;
    window->setWindowStates(fullScreenToggled(window->windowStates(), true));
  }

  void WindowMode::leaveFullScreen(QWindow* window) {
    if (!isFullScreen(window)) return;
    window->setWindowStates(fullScreenToggled(window->windowStates(), false));
  }

} // namespace jfd::ui
