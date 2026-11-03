#pragma once

#include <QString>

class QCoreApplication;

namespace jfd::desktop {

  // The interface language for the interface/language setting: "en", "ru",
  // or for "system" the first system UI language that has a translation
  // (English otherwise).
  QString resolveLanguage(const QString& setting);

  // Installs every :/i18n/*_<language>.qm: the modules' files and Qt's own.
  // English has only plural forms ("%n item(s)" as "1 item", "5 items").
  // Returns the number of files loaded.
  int installTranslations(QCoreApplication& app, const QString& language);

} // namespace jfd::desktop
