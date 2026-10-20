#pragma once

#include <QString>

class QCoreApplication;

namespace jfd::desktop {

  // The interface language for the interface/language setting: "en", "ru",
  // or for "system" the first system UI language that has a translation
  // (English otherwise).
  QString resolveLanguage(const QString& setting);

  // Installs every module's :/i18n/*_<language>.qm. English needs none.
  // Returns the number of files loaded.
  int installTranslations(QCoreApplication& app, const QString& language);

} // namespace jfd::desktop
