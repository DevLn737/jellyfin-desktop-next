#pragma once

#include <QList>
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

} // namespace jfd::ui
