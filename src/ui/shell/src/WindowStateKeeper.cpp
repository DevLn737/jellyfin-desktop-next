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
    connect(window, &QWindow::xChanged, this, &WindowStateKeeper::scheduleSave);
    connect(window, &QWindow::yChanged, this, &WindowStateKeeper::scheduleSave);
    connect(window, &QWindow::widthChanged, this, &WindowStateKeeper::scheduleSave);
    connect(window, &QWindow::heightChanged, this, &WindowStateKeeper::scheduleSave);
    connect(window, &QWindow::visibilityChanged, this, &WindowStateKeeper::scheduleSave);
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

    if (m_settings.value(keys::WindowFullScreen).toBool()) {
      m_window->showFullScreen();
    } else if (m_settings.value(keys::WindowMaximized).toBool()) {
      m_window->showMaximized();
    } else {
      m_window->show();
    }
    m_restored = true;
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
    // maximized nor full screen; keep the last one otherwise.
    if (visibility == QWindow::Windowed) {
      m_settings.setValue(keys::WindowGeometry, encodeGeometry(m_window->geometry()));
    }
    if (m_window->screen() != nullptr) {
      m_settings.setValue(keys::WindowScreen, m_window->screen()->name());
    }
    m_settings.setValue(keys::WindowMaximized, visibility == QWindow::Maximized);
    m_settings.setValue(keys::WindowFullScreen, visibility == QWindow::FullScreen);
  }

} // namespace jfd::ui
