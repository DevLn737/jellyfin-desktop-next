#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>
#include <QWindow>

#include "jfd/ui/WindowPlacement.h"

class QQuickWindow;

namespace jfd::settings {
  class Settings;
}

namespace jfd::ui {

  // Remembers the main window's size, position, screen, and maximized and
  // full-screen state in the window/* settings, and restores them at
  // startup with placeWindow() (a disconnected monitor moves the window to
  // one that is there). Leaving full screen brings back the size, position
  // and maximized state from before it.
  class WindowStateKeeper : public QObject {
    Q_OBJECT
  public:
    WindowStateKeeper(QQuickWindow* window, settings::Settings& settings, QObject* parent = nullptr);

    // Places and shows the window. Call once instead of show().
    void restoreAndShow(const QSize& defaultSize);
    // Writes the current state; also done automatically after changes.
    void save();

  private:
    void scheduleSave();
    void geometryChanged();
    void visibilityChanged(QWindow::Visibility visibility);

    QPointer<QQuickWindow> m_window;
    settings::Settings& m_settings;
    QTimer m_saveTimer;
    WindowShape m_shape;
    bool m_restored = false;
  };

} // namespace jfd::ui
