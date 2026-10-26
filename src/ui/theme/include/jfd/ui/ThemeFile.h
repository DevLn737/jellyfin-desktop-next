#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Themes as files: a small JSON document the user can write, share and drop
// into the themes folder. Only data is accepted, never a path or an
// address: colours, a corner radius and the name of an installed font
// family.
//
//   { "format": 1, "name": "Midnight", "base": "dark",
//     "colors": { "background": "#0b0e14", "accent": "#7aa2f7", ... },
//     "radius": 8, "fontFamily": "Inter" }
//
// Colours not given come from `base` ("dark", the default, or "light");
// keys that are not colour names are ignored; a colour that is not
// "#rgb", "#rrggbb" or "#aarrggbb" is an error. "format" must be 1.
namespace jfd::ui {

  inline constexpr qint64 MaxThemeFileBytes = 64 * 1024;
  inline constexpr int ThemeFormat = 1;

  struct ThemeDefinition {
    // The file's name without ".json"; what the "appearance/theme" setting
    // stores after "file:".
    QString id;
    QString name;
    // The base the missing colours come from, and which way hover and focus
    // colours move.
    bool darkBase = true;
    // Every colour name, complete.
    QVariantMap colors;
    // Whether the file gave "accent"; then the accent setting does not
    // override it.
    bool ownAccent = false;
    // -1: not given.
    int radius = -1;
    QString fontFamily;
  };

  struct ThemeLoadResult {
    bool ok = false;
    ThemeDefinition theme;
    // A theme with low contrast is accepted, with a warning for each
    // problem.
    QStringList warnings;
    // Translated, for the user, when !ok.
    QString error;
  };

  // `fallbackName` names a theme without a "name" (the file name). A
  // `fontFamily` that is not in `installedFonts` is dropped with a warning;
  // pass an empty list to accept any.
  ThemeLoadResult parseTheme(const QByteArray& json, const QString& fallbackName,
                             const QStringList& installedFonts = {});
  // The theme as a file, to give users a pattern.
  QByteArray exportTheme(const ThemeDefinition& theme);

} // namespace jfd::ui
