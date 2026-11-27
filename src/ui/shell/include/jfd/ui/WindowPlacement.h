#pragma once

#include <QList>
#include <QWindow>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace jfd::ui {

  struct ScreenArea {
    QString name;
    QRect available;
  };

  // "x,y,width,height" for the window/geometry setting.
  QString encodeGeometry(const QRect& geometry);
  std::optional<QRect> decodeGeometry(const QString& text);

  // Where to open the window. The saved geometry is used when its screen is
  // still connected and enough of the window would be visible to grab its
  // title bar. Otherwise the window keeps its saved size (shrunk to fit) and
  // is centred on the screen it was on, or on the primary screen when that
  // one is gone: the "monitor unplugged" case. With no saved geometry, it
  // opens at defaultSize centred on the primary screen.
  QRect placeWindow(const std::optional<QRect>& saved, const QString& savedScreen,
                    const QList<ScreenArea>& screens, int primaryScreen, const QSize& defaultSize);

  // The window states for full screen on or off. Maximized stays under
  // FullScreen, so leaving full screen goes back to maximized in one step
  // (Qt and the platforms keep both: FullScreen wins while it is set).
  // Minimized is dropped either way.
  Qt::WindowStates fullScreenToggled(Qt::WindowStates states, bool fullScreen);

  // What the window looked like before it went full screen. Leaving full
  // screen must bring back that size, position and maximized state: the
  // platform alone returns to a plain window, so a maximized window came
  // back as its (default-sized) normal rectangle.
  class WindowShape {
  public:
    // What to do once the window has left full screen.
    enum class Restore { Nothing, Maximize, Geometry };

    // Start from the state the window is shown in (the saved one).
    void start(const QRect& normalGeometry, bool maximized);

    // A window geometry change. Only a plain window's own rectangle counts:
    // not a maximized or full-screen one, and not one that already covers
    // its whole screen (the full-screen switch can report that rectangle
    // before the state change arrives).
    void geometryChanged(QWindow::Visibility visibility, const QRect& geometry, const QRect& screenGeometry);
    // A visibility change; says what to do when the window left full screen.
    Restore visibilityChanged(QWindow::Visibility visibility);

    // The plain window's last rectangle; invalid until one is known.
    QRect normalGeometry() const { return m_normal; }
    // Whether the window is (or was, before full screen) maximized.
    bool maximized() const { return m_maximized; }

  private:
    QRect m_normal;
    bool m_maximized = false;
    QWindow::Visibility m_last = QWindow::Hidden;
  };

} // namespace jfd::ui
