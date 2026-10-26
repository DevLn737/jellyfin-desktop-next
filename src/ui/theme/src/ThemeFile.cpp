#include "jfd/ui/ThemeFile.h"

#include "jfd/ui/ThemePalette.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

// lupdate finds the strings through the literal context.

namespace jfd::ui {

  namespace {

    ThemeLoadResult failure(const QString& message) {
      ThemeLoadResult result;
      result.error = message;
      return result;
    }

    // "#rgb" is expanded; the result is lower case.
    QString normalizedColor(const QString& text) {
      static const QRegularExpression pattern(
          QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
      const QString trimmed = text.trimmed();
      if (!pattern.match(trimmed).hasMatch()) {
        return {};
      }
      const QColor color(trimmed);
      return color.isValid() ? color.name(trimmed.size() == 9 ? QColor::HexArgb : QColor::HexRgb) : QString();
    }
  } // namespace

  ThemeLoadResult parseTheme(const QByteArray& json, const QString& fallbackName, const QStringList& installedFonts) {
    if (json.size() > MaxThemeFileBytes) {
      return failure(QCoreApplication::translate("Appearance", "The theme file is larger than 64 KiB."));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      return failure(QCoreApplication::translate("Appearance", "The theme file is not a valid JSON object."));
    }
    const QJsonObject root = document.object();

    const QJsonValue format = root.value(QStringLiteral("format"));
    if (!format.isDouble() || format.toInt() != format.toDouble()) {
      return failure(QCoreApplication::translate("Appearance", "The theme file has no format version."));
    }
    if (format.toInt() != ThemeFormat) {
      return failure(QCoreApplication::translate("Appearance", "The theme file was made for a newer version of the app."));
    }

    ThemeLoadResult result;
    ThemeDefinition& theme = result.theme;
    const QJsonValue name = root.value(QStringLiteral("name"));
    theme.name = name.isString() ? name.toString().trimmed().left(64) : QString();
    if (theme.name.isEmpty()) {
      theme.name = fallbackName;
    }

    const QJsonValue base = root.value(QStringLiteral("base"));
    if (!base.isUndefined() && base.toString() != QLatin1String("dark") && base.toString() != QLatin1String("light")) {
      return failure(QCoreApplication::translate("Appearance", "\"base\" must be \"dark\" or \"light\"."));
    }
    theme.darkBase = base.toString() != QLatin1String("light");
    theme.colors = theme.darkBase ? darkPalette() : lightPalette();

    const QJsonValue colors = root.value(QStringLiteral("colors"));
    if (!colors.isUndefined() && !colors.isObject()) {
      return failure(QCoreApplication::translate("Appearance", "\"colors\" must be an object."));
    }
    const QJsonObject given = colors.toObject();
    for (const QString& colorName : colorNames()) {
      if (!given.contains(colorName)) {
        continue;
      }
      const QString color = normalizedColor(given.value(colorName).toString());
      if (color.isEmpty()) {
        return failure(QCoreApplication::translate("Appearance", "The colour \"%1\" is not written as #rrggbb.").arg(colorName));
      }
      theme.colors.insert(colorName, color);
    }
    theme.ownAccent = given.contains(QStringLiteral("accent"));
    // The colours derived from the accent follow it unless the file sets
    // them itself.
    if (theme.ownAccent) {
      QVariantMap derived = withAccent(theme.colors, QColor(theme.colors.value(QStringLiteral("accent")).toString()),
                                       theme.darkBase);
      for (const char* derivedName : {"accentHover", "accentText", "focusRing", "progress"}) {
        if (!given.contains(QLatin1String(derivedName))) {
          theme.colors.insert(QLatin1String(derivedName), derived.value(QLatin1String(derivedName)));
        }
      }
    }

    const QJsonValue radius = root.value(QStringLiteral("radius"));
    if (!radius.isUndefined()) {
      if (!radius.isDouble() || radius.toInt() != radius.toDouble() || radius.toInt() < 0 || radius.toInt() > 24) {
        return failure(QCoreApplication::translate("Appearance", "\"radius\" must be a whole number from 0 to 24."));
      }
      theme.radius = radius.toInt();
    }

    const QJsonValue font = root.value(QStringLiteral("fontFamily"));
    if (!font.isUndefined()) {
      const QString family = font.toString().trimmed();
      if (!font.isString() || family.isEmpty() || family.size() > 64 || family.contains(QLatin1Char('/')) ||
          family.contains(QLatin1Char('\\')) || family.contains(QLatin1Char(':'))) {
        return failure(QCoreApplication::translate("Appearance", "\"fontFamily\" must be the name of an installed font."));
      }
      if (installedFonts.isEmpty() || installedFonts.contains(family, Qt::CaseInsensitive)) {
        theme.fontFamily = family;
      } else {
        result.warnings.append(QCoreApplication::translate("Appearance", "The font \"%1\" is not installed; the default font is used.").arg(family));
      }
    }

    for (const ContrastIssue& issue : contrastIssues(theme.colors)) {
      result.warnings.append(QCoreApplication::translate("Appearance", "Low contrast: %1 is %2:1, at least %3:1 is recommended.")
                                 .arg(QString::fromLatin1(issue.what.toLatin1()))
                                 .arg(issue.ratio, 0, 'f', 1)
                                 .arg(issue.required, 0, 'f', 1));
    }
    result.ok = true;
    return result;
  }

  QByteArray exportTheme(const ThemeDefinition& theme) {
    QJsonObject colors;
    for (const QString& name : colorNames()) {
      colors.insert(name, theme.colors.value(name).toString());
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), ThemeFormat);
    root.insert(QStringLiteral("name"), theme.name);
    root.insert(QStringLiteral("base"), theme.darkBase ? QStringLiteral("dark") : QStringLiteral("light"));
    root.insert(QStringLiteral("colors"), colors);
    if (theme.radius >= 0) {
      root.insert(QStringLiteral("radius"), theme.radius);
    }
    if (!theme.fontFamily.isEmpty()) {
      root.insert(QStringLiteral("fontFamily"), theme.fontFamily);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
  }

} // namespace jfd::ui
