#include "jfd/ui/WindowStateKeeper.h"

#include "jfd/ui/WindowPlacement.h"

#include <jfd/settings/CommonSections.h>
#include <jfd/settings/Settings.h>

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>

namespace jfd::ui {

  namespace keys = settings::keys;

  WindowStateKeeper::WindowStateKeeper(QQuickWindow* window, settings::Settings& settings, QObject* parent)
      : QObject(parent), m_window(window), m_settings(settings) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &WindowStateKeeper::save);
    connect(window, &QWindow::xChanged, this, &WindowStateKeeper::geometryChanged);
    connect(window, &QWindow::yChanged, this, &WindowStateKeeper::geometryChanged);
    connect(window, &QWindow::widthChanged, this, &WindowStateKeeper::geometryChanged);
    connect(window, &QWindow::heightChanged, this, &WindowStateKeeper::geometryChanged);
    connect(window, &QWindow::visibilityChanged, this, &WindowStateKeeper::visibilityChanged);
    connect(window, &QWindow::screenChanged, this, &WindowStateKeeper::scheduleSave);
  }

  void WindowStateKeeper::restoreAndShow(const QSize& defaultSize) {
    QList<ScreenArea> screens;
    int primary = 0;
    const QList<QScreen*> all = QGuiApplication::screens();
    for (QScreen* screen : all) {
      if (screen == QGuiApplication::primaryScreen()) {
        primary = int(screens.size());
      }
      screens.append({screen->name(), screen->availableGeometry()});
    }
    const QString savedScreen = m_settings.value(keys::WindowScreen).toString();
    const QRect rect = placeWindow(decodeGeometry(m_settings.value(keys::WindowGeometry).toString()),
                                   savedScreen, screens, primary, defaultSize);
    m_window->setGeometry(rect);
    m_shape.start(rect, m_settings.value(keys::WindowMaximized).toBool());

    if (m_settings.value(keys::WindowFullScreen).toBool()) {
      // Maximized stays under full screen, so leaving it is one step.
      m_window->setWindowStates(fullScreenToggled(
          m_settings.value(keys::WindowMaximized).toBool() ? Qt::WindowMaximized : Qt::WindowNoState, true));
      m_window->setVisible(true);
    } else if (m_settings.value(keys::WindowMaximized).toBool()) {
      m_window->showMaximized();
    } else {
      m_window->show();
    }
    m_restored = true;
  }

  void WindowStateKeeper::geometryChanged() {
    if (!m_window || !m_restored) return;
    const QRect screen = m_window->screen() != nullptr ? m_window->screen()->geometry() : QRect();
    m_shape.geometryChanged(m_window->visibility(), m_window->geometry(), screen);
    scheduleSave();
  }

  void WindowStateKeeper::visibilityChanged(QWindow::Visibility visibility) {
    const WindowShape::Restore restore = m_shape.visibilityChanged(visibility);
    if (restore != WindowShape::Restore::Nothing) {
      // After the platform has finished its own switch back to a window.
      // The rectangle is taken now: the platform's own one may arrive as a
      // geometry change before this runs.
      const QRect target = m_shape.normalGeometry();
      QTimer::singleShot(0, this, [this, restore, target] {
        if (!m_window || m_window->visibility() != QWindow::Windowed) return;
        if (restore == WindowShape::Restore::Maximize) {
          if (target.isValid()) m_window->setGeometry(target);
          m_window->showMaximized();
        } else if (m_window->geometry() != target) {
          m_window->setGeometry(target);
        }
      });
    }
    scheduleSave();
  }

  void WindowStateKeeper::scheduleSave() {
    if (m_restored) {
      m_saveTimer.start();
    }
  }

  void WindowStateKeeper::save() {
    m_saveTimer.stop();
    if (!m_window || !m_restored) {
      return;
    }
    const QWindow::Visibility visibility = m_window->visibility();
    if (visibility == QWindow::Hidden || visibility == QWindow::Minimized) {
      return;
    }
    // The normal geometry is only known while the window is neither
    // maximized nor full screen; m_shape keeps the last one.
    if (m_shape.normalGeometry().isValid()) {
      m_settings.setValue(keys::WindowGeometry, encodeGeometry(m_shape.normalGeometry()));
    }
    if (m_window->screen() != nullptr) {
      m_settings.setValue(keys::WindowScreen, m_window->screen()->name());
    }
    // Full screen keeps whether the window was maximized before it, so the
    // next start leaves full screen into the same shape.
    m_settings.setValue(keys::WindowMaximized,
                        visibility == QWindow::FullScreen ? m_shape.maximized() : visibility == QWindow::Maximized);
    m_settings.setValue(keys::WindowFullScreen, visibility == QWindow::FullScreen);
  }

} // namespace jfd::ui
