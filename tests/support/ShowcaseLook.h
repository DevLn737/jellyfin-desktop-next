#pragma once

// The look the application starts with, for screenshots of a screen as a
// person sees it: the interface in Russian, the Nunito font and the default
// palette (what ThemeInstall and the translations loader set). Undone when
// the object goes, so the test cases after it read the source strings.
//
// Create it before the QML page, or call retranslate() on its engine after.

#include <jfd/ui/ThemePalette.h>

#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLocale>
#include <QObject>
#include <QQmlEngine>
#include <QTranslator>

namespace jfd::test {

  class ShowcaseLook {
  public:
    ShowcaseLook() : m_locale(), m_font(QGuiApplication::font()) {
      for (const QString& file : QDir(QStringLiteral(":/i18n")).entryList({QStringLiteral("*_ru.qm")}, QDir::Files)) {
        auto* translator = new QTranslator(&m_translations);
        if (translator->load(QStringLiteral(":/i18n/") + file)) QCoreApplication::installTranslator(translator);
      }
      QLocale::setDefault(QLocale(QStringLiteral("ru")));
      for (const char* weight : {"Regular", "Medium", "SemiBold", "Bold", "ExtraBold", "Black"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Jfd/Ui/Theme/fonts/Nunito-") + QLatin1String(weight) +
                                          QStringLiteral(".ttf"));
      QGuiApplication::setFont(QFont(QStringLiteral("Nunito")));
    }

    ~ShowcaseLook() {
      for (auto* translator : m_translations.findChildren<QTranslator*>()) QCoreApplication::removeTranslator(translator);
      QLocale::setDefault(m_locale);
      QGuiApplication::setFont(m_font);
    }

    ShowcaseLook(const ShowcaseLook&) = delete;
    ShowcaseLook& operator=(const ShowcaseLook&) = delete;

    // The engine's Theme as the application sets it, and its strings again
    // in Russian.
    static void apply(QQmlEngine& engine) {
      if (QObject* theme = engine.singletonInstance<QObject*>("Jfd.Ui.Theme", "Theme")) {
        theme->setProperty("colors", ui::depthPalette());
        theme->setProperty("radiusOverride", 12);
        theme->setProperty("fontFamilyOverride", QStringLiteral("Nunito"));
      }
      engine.retranslate();
    }

  private:
    QObject m_translations;
    QLocale m_locale;
    QFont m_font;
  };

} // namespace jfd::test
