#include "../support/DoctestQt.h"

#include <jfd/ui/ThemeFile.h>
#include <jfd/ui/ThemePalette.h>

#include <QJsonDocument>
#include <QJsonObject>

using namespace jfd::ui;

namespace {

  QByteArray theme(const QByteArray& body) { return "{\"format\": 1, " + body + "}"; }

} // namespace

TEST_CASE("Contrast follows WCAG") {
  CHECK(contrastRatio(QColor(Qt::black), QColor(Qt::white)) == doctest::Approx(21.0));
  CHECK(contrastRatio(QColor(Qt::white), QColor(Qt::white)) == doctest::Approx(1.0));
  // The classic: #767676 on white is just over 4.5.
  CHECK(contrastRatio(QColor(QStringLiteral("#767676")), QColor(Qt::white)) == doctest::Approx(4.54).epsilon(0.01));
}

TEST_CASE("The built-in themes pass WCAG AA") {
  for (const QVariantMap& palette : {darkPalette(), lightPalette()}) {
    for (const ContrastIssue& issue : contrastIssues(palette)) {
      MESSAGE(issue.what.toStdString(), " ", issue.ratio);
    }
    CHECK(contrastIssues(palette).isEmpty());
    // Every colour name is there.
    for (const QString& name : colorNames()) {
      CHECK(palette.contains(name));
    }
    CHECK(contrastRatio(QColor(palette.value(QStringLiteral("text")).toString()),
                        QColor(palette.value(QStringLiteral("background")).toString())) >= 4.5);
    CHECK(contrastRatio(QColor(palette.value(QStringLiteral("textSecondary")).toString()),
                        QColor(palette.value(QStringLiteral("background")).toString())) >= 3.0);
  }
}

TEST_CASE("Any accent gives readable text on it and a visible focus ring, in both themes") {
  struct Base {
    QVariantMap palette;
    bool dark;
  };
  const QList<Base> bases = {{darkPalette(), true}, {lightPalette(), false}};
  QList<QColor> accents;
  for (const AccentPreset& preset : accentPresets()) {
    accents.append(preset.color);
  }
  // A sweep of colours, the awkward ones (mid-grey, pure primaries, near
  // black and white) included.
  for (int r = 0; r <= 255; r += 51) {
    for (int g = 0; g <= 255; g += 51) {
      for (int b = 0; b <= 255; b += 51) {
        accents.append(QColor(r, g, b));
      }
    }
  }
  accents.append(QColor(0x80, 0x80, 0x80));
  accents.append(QColor(0x76, 0x76, 0x76));
  for (const Base& base : bases) {
    for (const QColor& accent : accents) {
      const QVariantMap palette = withAccent(base.palette, accent, base.dark);
      const QColor accentFill(palette.value(QStringLiteral("accent")).toString());
      CHECK(accentFill == accent);
      const QColor onAccent(palette.value(QStringLiteral("accentText")).toString());
      CHECK_MESSAGE(contrastRatio(onAccent, accent) >= 4.5, accent.name().toStdString());
      const QColor ring(palette.value(QStringLiteral("focusRing")).toString());
      const QColor background(palette.value(QStringLiteral("background")).toString());
      const QColor surface(palette.value(QStringLiteral("surface")).toString());
      // Near black on a dark theme (or near white on a light one) cannot
      // reach 3:1 by moving its lightness a little: the ring must still
      // get there.
      CHECK_MESSAGE(contrastRatio(ring, background) >= 3.0, accent.name().toStdString());
      CHECK_MESSAGE(contrastRatio(ring, surface) >= 3.0, accent.name().toStdString());
    }
  }
}

TEST_CASE("Accent text is parsed strictly") {
  CHECK(parseAccent(QStringLiteral("#00A4DC")) == QColor(0x00, 0xa4, 0xdc));
  CHECK(parseAccent(QStringLiteral(" #0af ")) == QColor(0x00, 0xaa, 0xff));
  CHECK_FALSE(parseAccent(QStringLiteral("00a4dc")).isValid());
  CHECK_FALSE(parseAccent(QStringLiteral("red")).isValid());
  CHECK_FALSE(parseAccent(QStringLiteral("#12345")).isValid());
  CHECK_FALSE(parseAccent(QString()).isValid());
}

TEST_CASE("A correct theme file is applied over its base") {
  const auto result = parseTheme(
      theme(R"("name": "Midnight", "base": "dark", "colors": {"background": "#0b0e14", "surface": "#141824", "text": "#e6e9f2", "accent": "#7aa2f7", "unknownKey": "x"}, "radius": 10)"),
      QStringLiteral("midnight"));
  REQUIRE_MESSAGE(result.ok, result.error.toStdString());
  CHECK(result.theme.name == QStringLiteral("Midnight"));
  CHECK(result.theme.colors.value(QStringLiteral("background")).toString() == QStringLiteral("#0b0e14"));
  // Not given: from the base.
  CHECK(result.theme.colors.value(QStringLiteral("error")).toString() == darkPalette().value(QStringLiteral("error")).toString());
  CHECK(result.theme.ownAccent);
  CHECK(result.theme.colors.value(QStringLiteral("accent")).toString() == QStringLiteral("#7aa2f7"));
  CHECK(contrastRatio(QColor(result.theme.colors.value(QStringLiteral("accentText")).toString()), QColor(QStringLiteral("#7aa2f7"))) >= 4.5);
  CHECK(result.theme.radius == 10);
  CHECK(result.theme.colors.size() == colorNames().size());
  CHECK(result.warnings.isEmpty());

  // Written out and read back.
  const auto again = parseTheme(exportTheme(result.theme), QStringLiteral("x"));
  REQUIRE(again.ok);
  CHECK(again.theme.colors == result.theme.colors);
  CHECK(again.theme.radius == 10);
}

TEST_CASE("A theme without a name takes the file's, and short colours are expanded") {
  const auto result = parseTheme(theme(R"("colors": {"text": "#FFF"})"), QStringLiteral("plain"));
  REQUIRE(result.ok);
  CHECK(result.theme.name == QStringLiteral("plain"));
  CHECK(result.theme.colors.value(QStringLiteral("text")).toString() == QStringLiteral("#ffffff"));
}

TEST_CASE("Damaged, incomplete and newer theme files give a clear error and nothing else") {
  const auto fails = [](const QByteArray& json) {
    const auto result = parseTheme(json, QStringLiteral("t"));
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.isEmpty());
    return result;
  };
  fails("");
  fails("not json");
  fails("[1, 2, 3]");
  fails("{\"format\": 1,");
  // No format, a newer one, a fractional or text one.
  fails("{\"name\": \"x\"}");
  fails("{\"format\": 2, \"name\": \"x\"}");
  fails("{\"format\": 1.5}");
  fails("{\"format\": \"1\"}");
  // Bad values.
  fails(theme(R"("base": "blue")"));
  fails(theme(R"("colors": [])"));
  fails(theme(R"("colors": {"text": "red"})"));
  fails(theme(R"("colors": {"text": "#12345"})"));
  fails(theme(R"("colors": {"text": 5})"));
  fails(theme(R"("radius": -1)"));
  fails(theme(R"("radius": 25)"));
  fails(theme(R"("radius": 3.5)"));
  fails(theme(R"("radius": "big")"));
  // A font is a name: never a path or an address.
  fails(theme(R"("fontFamily": "/usr/share/fonts/x.ttf")"));
  fails(theme(R"("fontFamily": "C:\\Windows\\Fonts\\x.ttf")"));
  fails(theme(R"("fontFamily": "https://example.org/font")"));
  fails(theme(R"("fontFamily": 3)"));
  fails(theme(R"("fontFamily": "")"));
  // Too large.
  fails(theme("\"name\": \"" + QByteArray(MaxThemeFileBytes, 'a') + "\""));
}

TEST_CASE("A font that is not installed is dropped with a warning") {
  const QStringList installed = {QStringLiteral("Inter"), QStringLiteral("DejaVu Sans")};
  const auto have = parseTheme(theme(R"("fontFamily": "inter")"), QStringLiteral("t"), installed);
  REQUIRE(have.ok);
  CHECK(have.theme.fontFamily == QStringLiteral("inter"));
  const auto missing = parseTheme(theme(R"("fontFamily": "Comic Nonsense")"), QStringLiteral("t"), installed);
  REQUIRE(missing.ok);
  CHECK(missing.theme.fontFamily.isEmpty());
  CHECK(missing.warnings.size() == 1);
}

TEST_CASE("A theme with low contrast is accepted with a warning") {
  const auto result = parseTheme(
      theme(R"("colors": {"background": "#808080", "surface": "#858585", "text": "#8a8a8a"})"), QStringLiteral("t"));
  REQUIRE(result.ok);
  CHECK(result.warnings.size() >= 2);
}
