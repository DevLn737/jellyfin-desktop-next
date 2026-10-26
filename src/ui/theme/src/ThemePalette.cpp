#include "jfd/ui/ThemePalette.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace jfd::ui {

  namespace {
    QColor colorOf(const QVariantMap& palette, const char* name) {
      return QColor(palette.value(QLatin1String(name)).toString());
    }

    // "#rrggbb", or "#aarrggbb" when translucent.
    QString toText(const QColor& color) {
      return color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
    }

    double channel(int value) {
      const double c = value / 255.0;
      return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }

    QColor shiftLightness(const QColor& color, double delta) {
      float h, s, l, a;
      color.getHslF(&h, &s, &l, &a);
      return QColor::fromHslF(h < 0 ? 0 : h, s, static_cast<float>(std::clamp<double>(l + delta, 0.0, 1.0)), a);
    }

    // Moves the colour away from the background until it has `required`
    // contrast to every one of `against`, up to the extreme.
    QColor withContrast(QColor color, const QList<QColor>& against, double required, bool lighter) {
      const auto enough = [&](const QColor& candidate) {
        return std::all_of(against.begin(), against.end(),
                           [&](const QColor& other) { return contrastRatio(candidate, other) >= required; });
      };
      for (int i = 0; i < 40 && !enough(color); ++i) {
        color = shiftLightness(color, lighter ? 0.025 : -0.025);
      }
      return color;
    }
  } // namespace

  const QStringList& colorNames() {
    static const QStringList names = {
        QStringLiteral("background"), QStringLiteral("surface"),       QStringLiteral("surfaceRaised"),
        QStringLiteral("surfaceHover"), QStringLiteral("border"),      QStringLiteral("accent"),
        QStringLiteral("accentHover"), QStringLiteral("accentText"),   QStringLiteral("text"),
        QStringLiteral("textSecondary"), QStringLiteral("textDisabled"), QStringLiteral("error"),
        QStringLiteral("success"),     QStringLiteral("focusRing"),    QStringLiteral("scrim"),
        QStringLiteral("progress"),
    };
    return names;
  }

  namespace {
    QVariantMap rawDark() {
      return {
        {QStringLiteral("background"), QStringLiteral("#101010")},
        {QStringLiteral("surface"), QStringLiteral("#1c1c1c")},
        {QStringLiteral("surfaceRaised"), QStringLiteral("#262626")},
        {QStringLiteral("surfaceHover"), QStringLiteral("#303030")},
        {QStringLiteral("border"), QStringLiteral("#3a3a3a")},
        {QStringLiteral("accent"), QStringLiteral("#00a4dc")},
        {QStringLiteral("accentHover"), QStringLiteral("#33b6e3")},
        {QStringLiteral("accentText"), QStringLiteral("#ffffff")},
        {QStringLiteral("text"), QStringLiteral("#f2f2f2")},
        {QStringLiteral("textSecondary"), QStringLiteral("#b3b3b3")},
        {QStringLiteral("textDisabled"), QStringLiteral("#6b6b6b")},
        {QStringLiteral("error"), QStringLiteral("#e5534b")},
        {QStringLiteral("success"), QStringLiteral("#3fb950")},
        {QStringLiteral("focusRing"), QStringLiteral("#00a4dc")},
        {QStringLiteral("scrim"), QStringLiteral("#b0000000")},
        {QStringLiteral("progress"), QStringLiteral("#00a4dc")},
      };
    }

    QVariantMap rawLight() {
      return {
        {QStringLiteral("background"), QStringLiteral("#f4f5f7")},
        {QStringLiteral("surface"), QStringLiteral("#ffffff")},
        {QStringLiteral("surfaceRaised"), QStringLiteral("#eceef1")},
        {QStringLiteral("surfaceHover"), QStringLiteral("#e0e3e8")},
        {QStringLiteral("border"), QStringLiteral("#cfd3da")},
        {QStringLiteral("accent"), QStringLiteral("#0078a8")},
        {QStringLiteral("accentHover"), QStringLiteral("#006a94")},
        {QStringLiteral("accentText"), QStringLiteral("#ffffff")},
        {QStringLiteral("text"), QStringLiteral("#16181c")},
        {QStringLiteral("textSecondary"), QStringLiteral("#566070")},
        {QStringLiteral("textDisabled"), QStringLiteral("#9aa1ad")},
        {QStringLiteral("error"), QStringLiteral("#c62828")},
        {QStringLiteral("success"), QStringLiteral("#1b7f3b")},
        {QStringLiteral("focusRing"), QStringLiteral("#0078a8")},
        {QStringLiteral("scrim"), QStringLiteral("#b0000000")},
        {QStringLiteral("progress"), QStringLiteral("#0078a8")},
      };
    }
  } // namespace

  QVariantMap darkPalette() { return withAccent(rawDark(), accentPresets().first().color, true); }

  QVariantMap lightPalette() { return withAccent(rawLight(), accentPresets().first().color, false); }

  const QList<AccentPreset>& accentPresets() {
    static const QList<AccentPreset> presets = {
        {QStringLiteral("blue"), QT_TRANSLATE_NOOP("Appearance", "Blue"), QColor(0x00, 0xa4, 0xdc)},
        {QStringLiteral("purple"), QT_TRANSLATE_NOOP("Appearance", "Purple"), QColor(0x8e, 0x6b, 0xe8)},
        {QStringLiteral("pink"), QT_TRANSLATE_NOOP("Appearance", "Pink"), QColor(0xe0, 0x4f, 0x9a)},
        {QStringLiteral("red"), QT_TRANSLATE_NOOP("Appearance", "Red"), QColor(0xe5, 0x53, 0x4b)},
        {QStringLiteral("orange"), QT_TRANSLATE_NOOP("Appearance", "Orange"), QColor(0xf0, 0x8c, 0x2e)},
        {QStringLiteral("yellow"), QT_TRANSLATE_NOOP("Appearance", "Yellow"), QColor(0xe8, 0xc1, 0x2e)},
        {QStringLiteral("green"), QT_TRANSLATE_NOOP("Appearance", "Green"), QColor(0x3f, 0xb9, 0x50)},
        {QStringLiteral("teal"), QT_TRANSLATE_NOOP("Appearance", "Teal"), QColor(0x1f, 0xb5, 0xa8)},
    };
    return presets;
  }

  QColor parseAccent(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$"));
    const QString trimmed = text.trimmed();
    return pattern.match(trimmed).hasMatch() ? QColor(trimmed) : QColor();
  }

  double relativeLuminance(const QColor& color) {
    return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
  }

  double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
  }

  QVariantMap withAccent(QVariantMap base, const QColor& accent, bool darkBase) {
    const QColor white(0xff, 0xff, 0xff);
    const QColor black(0x00, 0x00, 0x00);
    const QColor background = colorOf(base, "background");
    const QColor surface = colorOf(base, "surface");

    const QColor onAccent = contrastRatio(accent, white) >= contrastRatio(accent, black) ? white : black;
    const QColor focus = withContrast(accent, {background, surface}, 3.0, darkBase);
    base.insert(QStringLiteral("accent"), toText(accent));
    base.insert(QStringLiteral("accentHover"), toText(shiftLightness(accent, darkBase ? 0.08 : -0.07)));
    base.insert(QStringLiteral("accentText"), toText(onAccent));
    base.insert(QStringLiteral("focusRing"), toText(focus));
    base.insert(QStringLiteral("progress"), toText(focus));
    return base;
  }

  QList<ContrastIssue> contrastIssues(const QVariantMap& palette) {
    const QColor background = colorOf(palette, "background");
    const QColor surface = colorOf(palette, "surface");
    const QColor text = colorOf(palette, "text");
    const QColor secondary = colorOf(palette, "textSecondary");
    const QColor accent = colorOf(palette, "accent");
    const QColor accentText = colorOf(palette, "accentText");
    const QColor focus = colorOf(palette, "focusRing");

    QList<ContrastIssue> issues;
    const auto check = [&](const char* what, const QColor& a, const QColor& b, double required) {
      const double ratio = contrastRatio(a, b);
      if (ratio < required) {
        issues.append({QString::fromLatin1(what), ratio, required});
      }
    };
    check("text on background", text, background, 4.5);
    check("text on surface", text, surface, 4.5);
    check("secondary text on background", secondary, background, 3.0);
    check("secondary text on surface", secondary, surface, 3.0);
    check("text on accent", accentText, accent, 4.5);
    check("focus ring on background", focus, background, 3.0);
    check("focus ring on surface", focus, surface, 3.0);
    return issues;
  }

} // namespace jfd::ui
