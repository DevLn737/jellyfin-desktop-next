#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Colour palettes as plain data: a map from the names in Theme.qml to colour
// strings ("#rrggbb", or "#aarrggbb" for translucent ones). The QML Theme
// shows whichever map the application gives it.
namespace jfd::ui {

  // The names every palette has, in the order Theme.qml lists them.
  const QStringList& colorNames();

  // The built-in palettes. `dark` matches Palettes.qml (a test checks).
  QVariantMap darkPalette();
  QVariantMap lightPalette();

  // An accent the user can pick from, by id ("blue", "green", ...).
  struct AccentPreset {
    QString id;
    // Source text; translated by the settings screen (context "Appearance").
    const char* label;
    QColor color;
  };
  const QList<AccentPreset>& accentPresets();
  // "#rrggbb" or "#rgb" (case-insensitive); an invalid colour otherwise.
  QColor parseAccent(const QString& text);

  // WCAG 2.x relative luminance and contrast ratio (1 to 21) of opaque
  // colours.
  double relativeLuminance(const QColor& color);
  double contrastRatio(const QColor& a, const QColor& b);

  // `base` with the accent applied: accent, accentHover, accentText (black
  // or white, whichever reads better on the accent), focusRing and progress.
  // `darkBase` tells which way "more visible" is: hover is lighter on dark
  // palettes and darker on light ones, and the focus ring is moved until it
  // has at least 3:1 contrast to the background and the surface.
  QVariantMap withAccent(QVariantMap base, const QColor& accent, bool darkBase);

  struct ContrastIssue {
    // "text on background", "focus ring on surface", ...
    QString what;
    double ratio = 0;
    double required = 0;
  };
  // Checks a palette against WCAG AA: text 4.5:1 and secondary text 3:1 on
  // the background and the surface, text on the accent 4.5:1, the focus ring
  // 3:1 on both. Empty when all pass.
  QList<ContrastIssue> contrastIssues(const QVariantMap& palette);

} // namespace jfd::ui
