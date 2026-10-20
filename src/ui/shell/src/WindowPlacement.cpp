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

} // namespace jfd::ui
