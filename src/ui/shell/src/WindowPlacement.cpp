#include "jfd/ui/WindowPlacement.h"

#include <QStringList>

#include <algorithm>

namespace jfd::ui {

  namespace {
    // How much of the window must be on screen to count as reachable.
    constexpr int MinimumVisibleWidth = 160;
    constexpr int MinimumVisibleHeight = 48;

    bool reachable(const QRect& window, const QRect& screen) {
      // The top strip (title bar) must be on the screen.
      const QRect titleBar(window.left(), window.top(), window.width(), MinimumVisibleHeight);
      const QRect visible = titleBar.intersected(screen);
      return visible.width() >= std::min(MinimumVisibleWidth, window.width()) &&
             visible.height() >= MinimumVisibleHeight;
    }

    QRect centred(QSize size, const QRect& screen) {
      size = size.boundedTo(screen.size());
      QRect rect(QPoint(0, 0), size);
      rect.moveCenter(screen.center());
      return rect;
    }
  } // namespace

  QString encodeGeometry(const QRect& geometry) {
    return QStringLiteral("%1,%2,%3,%4")
        .arg(geometry.x())
        .arg(geometry.y())
        .arg(geometry.width())
        .arg(geometry.height());
  }

  std::optional<QRect> decodeGeometry(const QString& text) {
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 4) {
      return std::nullopt;
    }
    int values[4] = {};
    for (int i = 0; i < 4; ++i) {
      bool ok = false;
      values[i] = parts[i].trimmed().toInt(&ok);
      if (!ok) {
        return std::nullopt;
      }
    }
    if (values[2] <= 0 || values[3] <= 0) {
      return std::nullopt;
    }
    return QRect(values[0], values[1], values[2], values[3]);
  }

  QRect placeWindow(const std::optional<QRect>& saved, const QString& savedScreen,
                    const QList<ScreenArea>& screens, int primaryScreen, const QSize& defaultSize) {
    if (screens.isEmpty()) {
      return saved.value_or(QRect(QPoint(0, 0), defaultSize));
    }
    const int primary = std::clamp(primaryScreen, 0, int(screens.size()) - 1);
    if (!saved) {
      return centred(defaultSize, screens[primary].available);
    }
    const auto onSaved = std::find_if(screens.begin(), screens.end(),
                                      [&](const ScreenArea& s) { return s.name == savedScreen; });
    if (onSaved != screens.end()) {
      const QRect area = onSaved->available;
      if (reachable(*saved, area)) {
        QRect rect = *saved;
        rect.setSize(rect.size().boundedTo(area.size()));
        return rect;
      }
      return centred(saved->size(), area);
    }
    return centred(saved->size(), screens[primary].available);
  }

  Qt::WindowStates fullScreenToggled(Qt::WindowStates states, bool fullScreen) {
    states.setFlag(Qt::WindowMinimized, false);
    states.setFlag(Qt::WindowFullScreen, fullScreen);
    return states;
  }

  void WindowShape::start(const QRect& normalGeometry, bool maximized) {
    m_normal = normalGeometry;
    m_maximized = maximized;
  }

  void WindowShape::geometryChanged(QWindow::Visibility visibility, const QRect& geometry,
                                    const QRect& screenGeometry) {
    if (visibility != QWindow::Windowed || !geometry.isValid()) return;
    if (screenGeometry.isValid() && geometry.contains(screenGeometry)) return;
    m_normal = geometry;
  }

  WindowShape::Restore WindowShape::visibilityChanged(QWindow::Visibility visibility) {
    const QWindow::Visibility previous = m_last;
    if (visibility == QWindow::Hidden || visibility == QWindow::Minimized) {
      return Restore::Nothing;
    }
    m_last = visibility;
    if (visibility == QWindow::Maximized) {
      m_maximized = true;
      return Restore::Nothing;
    }
    if (visibility == QWindow::FullScreen) {
      // m_maximized keeps what the window was before.
      return Restore::Nothing;
    }
    // A plain window now.
    if (previous != QWindow::FullScreen) {
      m_maximized = false;
      return Restore::Nothing;
    }
    if (m_maximized) return Restore::Maximize;
    return m_normal.isValid() ? Restore::Geometry : Restore::Nothing;
  }

} // namespace jfd::ui

