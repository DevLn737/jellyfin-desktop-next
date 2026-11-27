#pragma once

#include <QObject>
#include <qqmlintegration.h>

class QWindow;

// Full screen on and off for QML, one way for the whole application.
//
// Leaving full screen goes straight back to the shape the window had: a
// maximized window keeps Qt's Maximized state under FullScreen, so the
// platform switches from full screen to maximized in one step. Setting
// `visibility = Window.Windowed` instead showed the window's plain
// rectangle for a moment and only then maximized it again (a small window
// that stretches back on Windows).
namespace jfd::ui {

  class WindowMode : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

  public:
    explicit WindowMode(QObject* parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE static bool isFullScreen(QWindow* window);
    Q_INVOKABLE static void toggleFullScreen(QWindow* window);
    Q_INVOKABLE static void enterFullScreen(QWindow* window);
    Q_INVOKABLE static void leaveFullScreen(QWindow* window);
  };

} // namespace jfd::ui
