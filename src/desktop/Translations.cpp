#include "Translations.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QLoggingCategory>
#include <QTranslator>

namespace jfd::desktop {

  namespace {
    Q_LOGGING_CATEGORY(lcTranslations, "jfd.translations")

    const QStringList& translatedLanguages() {
      static const QStringList languages = {QStringLiteral("ru")};
      return languages;
    }
  } // namespace

  QString resolveLanguage(const QString& setting) {
    if (setting == QLatin1String("en") || translatedLanguages().contains(setting)) {
      return setting;
    }
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString& uiLanguage : uiLanguages) {
      const QString language = uiLanguage.section(QLatin1Char('-'), 0, 0).toLower();
      if (language == QLatin1String("en") || translatedLanguages().contains(language)) {
        return language;
      }
    }
    return QStringLiteral("en");
  }

  int installTranslations(QCoreApplication& app, const QString& language) {
    if (language == QLatin1String("en")) {
      return 0;
    }
    const QString suffix = QLatin1Char('_') + language + QStringLiteral(".qm");
    const QStringList files = QDir(QStringLiteral(":/i18n")).entryList({QLatin1Char('*') + suffix}, QDir::Files);
    int loaded = 0;
    for (const QString& file : files) {
      auto* translator = new QTranslator(&app);
      if (translator->load(QStringLiteral(":/i18n/") + file)) {
        QCoreApplication::installTranslator(translator);
        ++loaded;
      } else {
        qCWarning(lcTranslations) << "cannot load" << file;
        delete translator;
      }
    }
    qCInfo(lcTranslations) << "language" << language << "-" << loaded << "translation files";
    return loaded;
  }

} // namespace jfd::desktop
